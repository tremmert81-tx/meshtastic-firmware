# Cattle Presence Gateway

Python script to receive and display cattle tag presence data from Meshtastic mesh network. Automatically stores data to Supabase for web app access.

## Installation

1. Install Python 3.7 or later
2. Install dependencies:
   ```bash
   pip3 install -r requirements.txt
   ```

## Supabase Setup

### 1. Create a Supabase Project

1. Go to [supabase.com](https://supabase.com) and sign up/login
2. Click "New Project"
3. Fill in project details (name, database password, region)
4. Wait for project to be created (takes a few minutes)

### 2. Run Database Schema Migration

1. In Supabase Dashboard, go to **SQL Editor**
2. Click **New Query**
3. Copy and paste the contents of `supabase_schema.sql`
4. Click **Run** to execute the migration
5. Verify tables were created: Go to **Table Editor** and you should see:
   - `cattle_presence_reports`
   - `cattle_tag_readings`
   - `recent_tag_activity` (view)

### 3. Get API Credentials

1. In Supabase Dashboard, go to **Settings** → **API**
2. Copy the following values:
   - **Project URL** (e.g., `https://xxxxx.supabase.co`)
   - **service_role key** (the secret key, not the anon key)

### 4. Configure Environment Variables

1. Copy the example environment file:
   ```bash
   cp .env.example .env
   ```

2. Edit `.env` and fill in your Supabase credentials:
   ```bash
   SUPABASE_URL=https://your-project-id.supabase.co
   SUPABASE_KEY=your-service-role-key-here
   SUPABASE_ENABLED=true
   ```

3. **Important**: Never commit `.env` to version control (it's already in `.gitignore`)

### 5. Enable Real-time (for Web App)

1. In Supabase Dashboard, go to **Database** → **Replication**
2. Enable replication for:
   - `cattle_presence_reports`
   - `cattle_tag_readings`
3. This allows your web app to subscribe to real-time updates

## Usage

### Connect via Serial Port (USB)
```bash
python3 cattle_gateway.py --port /dev/ttyUSB0
```

On Windows:
```bash
python3 cattle_gateway.py --port COM3
```

### Connect via TCP (Network)
If your Meshtastic node is connected to the network:
```bash
python3 cattle_gateway.py --host meshtastic.local
# or
python3 cattle_gateway.py --host 192.168.1.100
```

### Auto-detect
If you have only one Meshtastic node connected:
```bash
python3 cattle_gateway.py
```

## Output

The script displays cattle presence reports in the terminal:

```
============================================================
[2024-01-15 14:30:25] Cattle Presence Report
============================================================
From Node: 0x58D0AB2 (ID: 2738)
Tags Detected: 3
------------------------------------------------------------
Tag ID     RSSI (dBm)  Status     Distance Est   
------------------------------------------------------------
30711      -62         0x01       1-5m          
30860      -68         0x01       1-5m          
30760      -82         0x01       5-20m         
============================================================
✓ Stored 3 tag readings to Supabase (report_id: a1b2c3d4...)
```

## Supabase Integration

The script automatically stores all cattle presence data to Supabase:

- **Reports**: Each cattle presence report is stored in `cattle_presence_reports`
- **Tag Readings**: Individual tag readings are stored in `cattle_tag_readings`
- **Real-time**: Data is available immediately via Supabase real-time subscriptions
- **Historical**: All data is preserved for historical analysis

### Querying Data from Web App

Your web app can query Supabase using the Supabase JavaScript client:

```javascript
// Get recent reports
const { data, error } = await supabase
  .from('cattle_presence_reports')
  .select('*, cattle_tag_readings(*)')
  .order('received_at', { ascending: false })
  .limit(10);

// Subscribe to real-time updates
supabase
  .channel('cattle-updates')
  .on('postgres_changes', 
    { event: 'INSERT', schema: 'public', table: 'cattle_presence_reports' },
    (payload) => {
      console.log('New cattle report:', payload.new);
    }
  )
  .subscribe();
```

### Viewing Data in Supabase Dashboard

1. Go to **Table Editor** in Supabase Dashboard
2. Select `cattle_presence_reports` or `cattle_tag_readings`
3. View all stored data in real-time

## Configuration Options

Edit `.env` to customize behavior:

- `SUPABASE_ENABLED=false` - Disable Supabase (for testing without database)
- `SUPABASE_MAX_RETRIES=3` - Number of retry attempts for failed operations
- `SUPABASE_RETRY_DELAY=2` - Delay between retries (seconds)

## Troubleshooting

### "meshtastic library not installed"
Install it with: `pip3 install meshtastic`

### "supabase or python-dotenv not installed"
Install dependencies: `pip3 install -r requirements.txt`

### "Permission denied" on serial port
On Linux, add your user to the dialout group:
```bash
sudo usermod -a -G dialout $USER
# Then log out and back in
```

### "No packets received"
- Check that your Meshtastic node is powered and connected
- Verify the node is on the same mesh network as the cattle tracking nodes
- Check that cattle tracking nodes are actually sending data (check their serial output)
- Try increasing the report interval if nodes are configured for long intervals

### "Supabase client initialization failed"
- Verify your `.env` file exists and contains correct credentials
- Check that `SUPABASE_URL` and `SUPABASE_KEY` are set correctly
- Ensure you're using the **service_role** key (not anon key) for inserting data
- Check your internet connection (Pi Zero W needs WiFi/Ethernet)

### "Failed to insert to Supabase"
- Check Supabase dashboard for any errors or rate limits
- Verify the database schema was created correctly
- Check network connectivity from Raspberry Pi
- Review error messages in the terminal for specific issues
- The script will retry automatically (configurable in `.env`)

### Supabase data not appearing
- Check Supabase Dashboard → Table Editor to verify data is being inserted
- Verify real-time is enabled in Database → Replication
- Check RLS policies if you're querying from a web app (may need to adjust policies)

