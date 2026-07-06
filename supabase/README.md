# BirdWatch Cam — cloud backend (Phase 2)

Turns the camera from a Local Only device into one that publishes to a public
website you can open from any phone, anywhere. The camera makes only
*outbound* calls, so it works behind home/school Wi-Fi with no router setup.

## One-time setup (about 10 minutes, free tier)

1. **Create a Supabase project** at https://supabase.com (free). Note the
   project URL (`https://xxxx.supabase.co`) and the **anon public** key
   (Settings → API).
2. **Run the schema:** open the SQL editor, paste all of `schema.sql`, run it.
   Creates the tables, locks them with row-level security, and adds the
   device-authenticated publish functions.
3. **Register this camera** (SQL editor) — pick a long random key and keep it:
   ```sql
   select register_device('yst-nj-lab', 'Science lab window',
                           'northern NJ', 'cloud', 'a-long-random-device-key');
   ```
4. **Point the camera at it:** in the camera's Configuration tab set the
   Supabase URL, the anon key, and the same device key, and switch sharing
   mode to **Full Cloud**. (Firmware publish step — in progress.)
5. **Deploy the public site** (`web/public/`, in progress) to Netlify/Vercel
   with the project URL + anon key as config. That URL is what you open on
   your phone.

## How the pieces talk

```
camera  --submit_detection(device_key,...)-->  Supabase RPC  -->  detections table
        --submit_heartbeat(device_key,...)-->                     heartbeats table
public site  --anon read (RLS: published only)-->  detections / heartbeats
```

The anon key is public (it ships in the website) and can only *read* published
rows — it cannot write. Writes require the per-device key, checked server-side
inside the functions, which also enforce that only Full Cloud devices publish
birds. See `schema.sql` for the policies.

## What's built vs. pending

- [x] `schema.sql` — tables, RLS, publish functions, provisioning helper
- [ ] firmware publish step (POST detections + heartbeats when mode = cloud)
- [ ] TLS: enable real certificate validation before sending keys over the internet
- [ ] `web/public/` — the public site reading from Supabase
- [ ] thumbnails of real captures (v1 renders the bundled illustration per species)
