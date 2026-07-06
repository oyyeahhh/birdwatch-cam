-- BirdWatch Cam — Supabase schema (Phase 2, push-to-cloud backend)
--
-- Run this once in the Supabase SQL editor on a fresh project. It creates
-- the three tables from docs/DESIGN.md (devices, detections, heartbeats),
-- locks them down with row-level security, and exposes two SECURITY DEFINER
-- functions the camera calls to publish — so a camera authenticates with its
-- own per-device key, never a table-write grant.
--
-- Security model:
--   * The public (anon) role can READ published detections and health for
--     devices that opted into sharing, and nothing else. It CANNOT write.
--   * A camera publishes by calling submit_detection()/submit_heartbeat()
--     with its device key. The functions hash-check the key and enforce the
--     device's sharing_mode, so privacy doesn't depend on the firmware alone
--     (design doc §8: server-side mode enforcement).
--   * The api_key_hash column is never exposed to anon (reads go through a
--     view that omits it).

create extension if not exists pgcrypto;   -- for digest() (sha256 key hashing)

-- ─────────────────────────────── devices ───────────────────────────────
create table if not exists devices (
  device_id        text primary key,              -- org-site-window, e.g. yst-nj-lab
  name             text not null default '',
  location         text not null default '',
  sharing_mode     text not null default 'local'
                     check (sharing_mode in ('local','diag','cloud')),
  api_key_hash     text not null,                 -- sha256 hex of the device key
  firmware_version text not null default '',
  last_heartbeat   timestamptz,
  created_at       timestamptz not null default now()
);

-- ────────────────────────────── detections ─────────────────────────────
create table if not exists detections (
  id              bigint generated always as identity primary key,
  device_id       text not null references devices(device_id) on delete cascade,
  local_id        bigint,                          -- the camera's own IMG_<n> id
  seen_at         timestamptz not null,
  common_name     text not null,
  scientific_name text not null default '',
  confidence_pct  int  not null check (confidence_pct between 0 and 100),
  count           int  not null default 1,
  notes           text not null default '',
  image_url       text,                            -- storage URL if a thumbnail was uploaded (v1: null, site uses the bundled illustration)
  published       boolean not null default true,   -- false = below review threshold, held from the public gallery
  created_at      timestamptz not null default now(),
  unique (device_id, local_id)                     -- idempotent re-publish / retry from the SD queue
);
create index if not exists detections_device_seen_idx on detections (device_id, seen_at desc);

-- ────────────────────────────── heartbeats ─────────────────────────────
create table if not exists heartbeats (
  id                bigint generated always as identity primary key,
  device_id         text not null references devices(device_id) on delete cascade,
  ts                timestamptz not null default now(),
  uptime_s          int,
  rssi              int,
  free_heap         int,
  lux               real,
  triggers_today    int,
  api_calls_today   int,
  api_cap           int,
  identified_today  int,
  published_today   int,
  firmware_version  text,
  errors            text
);
create index if not exists heartbeats_device_ts_idx on heartbeats (device_id, ts desc);

-- ─────────────────────────── public read view ──────────────────────────
-- Devices exposed to the public site — omits api_key_hash. Only cloud+diag
-- devices appear (a Local Only device is invisible to the cloud entirely).
create or replace view public_devices as
  select device_id, name, location, sharing_mode, firmware_version, last_heartbeat
  from devices
  where sharing_mode in ('cloud','diag');

-- ──────────────────────────── row-level security ───────────────────────
alter table devices    enable row level security;
alter table detections enable row level security;
alter table heartbeats enable row level security;

-- Published birds from Full Cloud devices are world-readable; nothing else is.
drop policy if exists "anon reads published detections" on detections;
create policy "anon reads published detections" on detections
  for select to anon
  using (published = true
         and device_id in (select device_id from devices where sharing_mode = 'cloud'));

-- Health is readable for both cloud and diagnostics-only devices (the fleet view).
drop policy if exists "anon reads heartbeats" on heartbeats;
create policy "anon reads heartbeats" on heartbeats
  for select to anon
  using (device_id in (select device_id from devices where sharing_mode in ('cloud','diag')));

-- No anon policy on devices: the table is private; the site reads public_devices.
-- No anon insert/update/delete anywhere: writes only via the functions below.

-- ─────────────────── device-authenticated write functions ──────────────
-- The camera POSTs to /rest/v1/rpc/submit_detection with the anon apikey
-- header (just to reach PostgREST) and the device key in the body. The anon
-- key can't touch the tables directly — only invoke these, which validate.

create or replace function submit_detection(
  p_device text, p_key text, p_local_id bigint, p_seen_at timestamptz,
  p_common text, p_sci text, p_conf int, p_count int, p_notes text, p_published boolean
) returns bigint
language plpgsql security definer set search_path = public as $$
declare v_mode text; v_id bigint;
begin
  select sharing_mode into v_mode from devices
   where device_id = p_device
     and api_key_hash = encode(digest(p_key,'sha256'),'hex');
  if v_mode is null then raise exception 'unknown device or bad key'; end if;
  if v_mode <> 'cloud' then raise exception 'device % is not in cloud mode', p_device; end if;

  insert into detections(device_id, local_id, seen_at, common_name, scientific_name,
                         confidence_pct, count, notes, published)
  values (p_device, p_local_id, p_seen_at, p_common, coalesce(p_sci,''),
          p_conf, coalesce(p_count,1), coalesce(p_notes,''), coalesce(p_published,true))
  on conflict (device_id, local_id) do update
     set common_name = excluded.common_name, confidence_pct = excluded.confidence_pct,
         published = excluded.published
  returning id into v_id;
  return v_id;
end $$;

create or replace function submit_heartbeat(
  p_device text, p_key text, p_uptime int, p_rssi int, p_free_heap int, p_lux real,
  p_triggers int, p_api_calls int, p_api_cap int, p_identified int, p_published int,
  p_fw text, p_errors text
) returns void
language plpgsql security definer set search_path = public as $$
declare v_mode text;
begin
  select sharing_mode into v_mode from devices
   where device_id = p_device
     and api_key_hash = encode(digest(p_key,'sha256'),'hex');
  if v_mode is null then raise exception 'unknown device or bad key'; end if;
  if v_mode not in ('cloud','diag') then raise exception 'device % does not report health', p_device; end if;

  insert into heartbeats(device_id, uptime_s, rssi, free_heap, lux, triggers_today,
                         api_calls_today, api_cap, identified_today, published_today,
                         firmware_version, errors)
  values (p_device, p_uptime, p_rssi, p_free_heap, p_lux, p_triggers,
          p_api_calls, p_api_cap, p_identified, p_published, p_fw, p_errors);
  update devices set last_heartbeat = now(), firmware_version = coalesce(p_fw, firmware_version)
   where device_id = p_device;
end $$;

-- ─────────────────────────── provisioning helper ───────────────────────
-- Run once per camera FROM THE DASHBOARD (not callable by anon) to register
-- a device and set its key. Example:
--   select register_device('yst-nj-lab','Science lab window','northern NJ','cloud','choose-a-long-random-key');
-- Give that same key to the camera in its Configuration tab.
create or replace function register_device(
  p_device text, p_name text, p_location text, p_mode text, p_key text
) returns void
language sql security definer set search_path = public as $$
  insert into devices(device_id, name, location, sharing_mode, api_key_hash)
  values (p_device, p_name, p_location, p_mode, encode(digest(p_key,'sha256'),'hex'))
  on conflict (device_id) do update
     set name = excluded.name, location = excluded.location,
         sharing_mode = excluded.sharing_mode, api_key_hash = excluded.api_key_hash;
$$;
revoke execute on function register_device(text,text,text,text,text) from anon, authenticated;
