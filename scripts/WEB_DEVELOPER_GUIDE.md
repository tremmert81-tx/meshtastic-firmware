# Cattle Tracking System - Web Developer Guide

## Overview

This document provides everything you need to build a web frontend for the cattle tracking system. The system tracks cattle via BLE tags, sends data over a Meshtastic mesh network, and stores it in Supabase for real-time web access.

## Architecture

```
Cattle Tags (BLE) → Meshtastic Nodes → Gateway (Raspberry Pi) → Supabase → Web App
```

- **Cattle Tags**: BLE devices attached to cattle, broadcasting with MAC prefix `BC:XX:XX:...`
- **Meshtastic Nodes**: LoRa mesh network nodes that detect tags and send presence reports
- **Gateway**: Python script running on Raspberry Pi that receives mesh data and stores to Supabase
- **Supabase**: PostgreSQL database with real-time capabilities
- **Web App**: Your frontend that queries and subscribes to Supabase

## Supabase Setup

### 1. Get Your Supabase Credentials

1. Go to your Supabase project dashboard
2. Navigate to **Settings** → **API**
3. Copy these values:
   - **Project URL** (e.g., `https://xxxxx.supabase.co`)
   - **anon/public key** (for client-side queries with RLS)
   - **service_role key** (for server-side/admin operations - keep secret!)

### 2. Install Supabase Client

```bash
npm install @supabase/supabase-js
# or
yarn add @supabase/supabase-js
```

### 3. Initialize Supabase Client

```javascript
import { createClient } from '@supabase/supabase-js'

const supabaseUrl = 'https://your-project-id.supabase.co'
const supabaseAnonKey = 'your-anon-key-here'

export const supabase = createClient(supabaseUrl, supabaseAnonKey)
```

## Database Schema

### Tables

#### `cattle_presence_reports`
Stores each cattle presence report received from a Meshtastic node.

| Column | Type | Description |
|--------|------|-------------|
| `id` | UUID | Primary key |
| `node_id` | INTEGER | Meshtastic node ID (short ID) |
| `from_node_hex` | TEXT | Node hex address (e.g., "0x58D0AB2") |
| `tag_count` | INTEGER | Number of tags in this report |
| `received_at` | TIMESTAMPTZ | When the report was received by gateway |
| `created_at` | TIMESTAMPTZ | Record creation timestamp |

#### `cattle_tag_readings`
Stores individual tag readings from each report.

| Column | Type | Description |
|--------|------|-------------|
| `id` | UUID | Primary key |
| `report_id` | UUID | Foreign key → `cattle_presence_reports.id` |
| `tag_id` | INTEGER | Cattle tag ID (derived from BLE MAC address) |
| `rssi` | INTEGER | Signal strength in dBm (e.g., -55, -80) |
| `status` | INTEGER | Tag status flags (currently 0x01) |
| `distance_estimate` | TEXT | Estimated distance ("< 1m", "1-5m", "5-20m", "> 20m") |
| `created_at` | TIMESTAMPTZ | Record creation timestamp |

#### `recent_tag_activity` (View)
Convenience view joining reports and readings for easy querying.

## Querying Data

### Get Recent Reports with Tag Readings

```javascript
// Get last 10 reports with all their tag readings
const { data, error } = await supabase
  .from('cattle_presence_reports')
  .select(`
    *,
    cattle_tag_readings (*)
  `)
  .order('received_at', { ascending: false })
  .limit(10)

if (error) {
  console.error('Error fetching reports:', error)
} else {
  console.log('Reports:', data)
}
```

### Get All Tags Seen in Last Hour

```javascript
const oneHourAgo = new Date(Date.now() - 60 * 60 * 1000).toISOString()

const { data, error } = await supabase
  .from('cattle_tag_readings')
  .select('*')
  .gte('created_at', oneHourAgo)
  .order('created_at', { ascending: false })

if (error) {
  console.error('Error fetching recent tags:', error)
} else {
  console.log('Recent tags:', data)
}
```

### Get Latest Reading for Each Tag

```javascript
// Get the most recent reading for each unique tag_id
const { data, error } = await supabase
  .from('cattle_tag_readings')
  .select('*')
  .order('created_at', { ascending: false })

if (data) {
  // Group by tag_id and get the first (most recent) for each
  const latestByTag = {}
  data.forEach(reading => {
    if (!latestByTag[reading.tag_id] || 
        new Date(reading.created_at) > new Date(latestByTag[reading.tag_id].created_at)) {
      latestByTag[reading.tag_id] = reading
    }
  })
  
  const uniqueTags = Object.values(latestByTag)
  console.log('Latest readings per tag:', uniqueTags)
}
```

### Get Tags by Node

```javascript
// Get all reports from a specific node
const nodeHex = '0x58D0AB2'

const { data, error } = await supabase
  .from('cattle_presence_reports')
  .select(`
    *,
    cattle_tag_readings (*)
  `)
  .eq('from_node_hex', nodeHex)
  .order('received_at', { ascending: false })
  .limit(20)
```

### Get Tag History

```javascript
// Get all readings for a specific tag over time
const tagId = 4575

const { data, error } = await supabase
  .from('cattle_tag_readings')
  .select(`
    *,
    cattle_presence_reports (
      from_node_hex,
      node_id,
      received_at
    )
  `)
  .eq('tag_id', tagId)
  .order('created_at', { ascending: false })
  .limit(100)
```

## Real-Time Subscriptions

### Subscribe to New Reports

```javascript
// Listen for new cattle presence reports in real-time
const channel = supabase
  .channel('cattle-reports')
  .on(
    'postgres_changes',
    {
      event: 'INSERT',
      schema: 'public',
      table: 'cattle_presence_reports'
    },
    (payload) => {
      console.log('New report received:', payload.new)
      // Fetch the associated tag readings
      fetchTagReadingsForReport(payload.new.id)
    }
  )
  .subscribe()

// Don't forget to unsubscribe when done
// channel.unsubscribe()
```

### Subscribe to New Tag Readings

```javascript
// Listen for new tag readings in real-time
const channel = supabase
  .channel('cattle-readings')
  .on(
    'postgres_changes',
    {
      event: 'INSERT',
      schema: 'public',
      table: 'cattle_tag_readings'
    },
    (payload) => {
      console.log('New tag reading:', payload.new)
      // Update your UI with the new reading
      updateTagOnMap(payload.new)
    }
  )
  .subscribe()
```

### Combined Real-Time Subscription

```javascript
// Subscribe to both reports and readings
const reportsChannel = supabase
  .channel('cattle-updates')
  .on(
    'postgres_changes',
    {
      event: 'INSERT',
      schema: 'public',
      table: 'cattle_presence_reports'
    },
    handleNewReport
  )
  .on(
    'postgres_changes',
    {
      event: 'INSERT',
      schema: 'public',
      table: 'cattle_tag_readings'
    },
    handleNewReading
  )
  .subscribe()
```

## Data Interpretation

### RSSI Values

RSSI (Received Signal Strength Indicator) is measured in dBm (decibels relative to milliwatt):

- **-30 to -50 dBm**: Very close (< 1m) - Tag is very near the node
- **-50 to -70 dBm**: Close (1-5m) - Tag is nearby
- **-70 to -90 dBm**: Medium (5-20m) - Tag is at moderate distance
- **-90 to -110 dBm**: Far (> 20m) - Tag is far away or signal is weak

**Note**: Lower (more negative) RSSI values indicate weaker signal and greater distance.

### Tag IDs

Tag IDs are derived from the last 2 bytes of the BLE MAC address:
- MAC: `BC:57:29:0A:77:F7` → Tag ID: `30711` (0x77F7)
- MAC: `BC:57:29:05:11:DF` → Tag ID: `4575` (0x11DF)

### Status Flags

Currently, status is always `0x01`. Future versions may use this for:
- Battery level indicators
- Motion detection
- Health alerts
- etc.

### Distance Estimates

The gateway automatically calculates distance estimates based on RSSI:
- `"< 1m"` for RSSI >= -50
- `"1-5m"` for RSSI >= -70
- `"5-20m"` for RSSI >= -90
- `"> 20m"` for RSSI < -90

## Example React Component

```jsx
import { useEffect, useState } from 'react'
import { supabase } from './supabase'

function CattleDashboard() {
  const [reports, setReports] = useState([])
  const [latestTags, setLatestTags] = useState({})

  useEffect(() => {
    // Load initial data
    loadRecentReports()

    // Subscribe to real-time updates
    const channel = supabase
      .channel('cattle-updates')
      .on(
        'postgres_changes',
        {
          event: 'INSERT',
          schema: 'public',
          table: 'cattle_presence_reports'
        },
        async (payload) => {
          // Fetch the full report with readings
          const { data } = await supabase
            .from('cattle_presence_reports')
            .select('*, cattle_tag_readings(*)')
            .eq('id', payload.new.id)
            .single()
          
          if (data) {
            setReports(prev => [data, ...prev])
            updateLatestTags(data.cattle_tag_readings)
          }
        }
      )
      .subscribe()

    return () => {
      channel.unsubscribe()
    }
  }, [])

  const loadRecentReports = async () => {
    const { data, error } = await supabase
      .from('cattle_presence_reports')
      .select('*, cattle_tag_readings(*)')
      .order('received_at', { ascending: false })
      .limit(20)

    if (error) {
      console.error('Error loading reports:', error)
    } else {
      setReports(data)
      // Update latest tags
      const tags = {}
      data.forEach(report => {
        report.cattle_tag_readings?.forEach(reading => {
          if (!tags[reading.tag_id] || 
              new Date(reading.created_at) > new Date(tags[reading.tag_id].created_at)) {
            tags[reading.tag_id] = reading
          }
        })
      })
      setLatestTags(tags)
    }
  }

  const updateLatestTags = (readings) => {
    setLatestTags(prev => {
      const updated = { ...prev }
      readings.forEach(reading => {
        if (!updated[reading.tag_id] || 
            new Date(reading.created_at) > new Date(updated[reading.tag_id].created_at)) {
          updated[reading.tag_id] = reading
        }
      })
      return updated
    })
  }

  return (
    <div>
      <h1>Cattle Tracking Dashboard</h1>
      
      <section>
        <h2>Active Tags ({Object.keys(latestTags).length})</h2>
        <div className="tags-grid">
          {Object.values(latestTags).map(tag => (
            <div key={tag.id} className="tag-card">
              <h3>Tag #{tag.tag_id}</h3>
              <p>RSSI: {tag.rssi} dBm</p>
              <p>Distance: {tag.distance_estimate}</p>
              <p>Last seen: {new Date(tag.created_at).toLocaleString()}</p>
            </div>
          ))}
        </div>
      </section>

      <section>
        <h2>Recent Reports</h2>
        {reports.map(report => (
          <div key={report.id} className="report-card">
            <h3>Report from {report.from_node_hex}</h3>
            <p>{report.tag_count} tags detected at {new Date(report.received_at).toLocaleString()}</p>
            <ul>
              {report.cattle_tag_readings?.map(reading => (
                <li key={reading.id}>
                  Tag {reading.tag_id}: {reading.rssi} dBm ({reading.distance_estimate})
                </li>
              ))}
            </ul>
          </div>
        ))}
      </section>
    </div>
  )
}

export default CattleDashboard
```

## Example Vue Component

```vue
<template>
  <div>
    <h1>Cattle Tracking Dashboard</h1>
    
    <section>
      <h2>Active Tags ({{ Object.keys(latestTags).length }})</h2>
      <div class="tags-grid">
        <div v-for="tag in Object.values(latestTags)" :key="tag.id" class="tag-card">
          <h3>Tag #{{ tag.tag_id }}</h3>
          <p>RSSI: {{ tag.rssi }} dBm</p>
          <p>Distance: {{ tag.distance_estimate }}</p>
          <p>Last seen: {{ formatDate(tag.created_at) }}</p>
        </div>
      </div>
    </section>

    <section>
      <h2>Recent Reports</h2>
      <div v-for="report in reports" :key="report.id" class="report-card">
        <h3>Report from {{ report.from_node_hex }}</h3>
        <p>{{ report.tag_count }} tags detected at {{ formatDate(report.received_at) }}</p>
        <ul>
          <li v-for="reading in report.cattle_tag_readings" :key="reading.id">
            Tag {{ reading.tag_id }}: {{ reading.rssi }} dBm ({{ reading.distance_estimate }})
          </li>
        </ul>
      </div>
    </section>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { supabase } from './supabase'

const reports = ref([])
const latestTags = ref({})
let channel = null

onMounted(async () => {
  await loadRecentReports()
  
  // Subscribe to real-time updates
  channel = supabase
    .channel('cattle-updates')
    .on(
      'postgres_changes',
      {
        event: 'INSERT',
        schema: 'public',
        table: 'cattle_presence_reports'
      },
      async (payload) => {
        const { data } = await supabase
          .from('cattle_presence_reports')
          .select('*, cattle_tag_readings(*)')
          .eq('id', payload.new.id)
          .single()
        
        if (data) {
          reports.value.unshift(data)
          updateLatestTags(data.cattle_tag_readings)
        }
      }
    )
    .subscribe()
})

onUnmounted(() => {
  if (channel) {
    channel.unsubscribe()
  }
})

const loadRecentReports = async () => {
  const { data, error } = await supabase
    .from('cattle_presence_reports')
    .select('*, cattle_tag_readings(*)')
    .order('received_at', { ascending: false })
    .limit(20)

  if (error) {
    console.error('Error loading reports:', error)
  } else {
    reports.value = data
    updateLatestTagsFromReports(data)
  }
}

const updateLatestTagsFromReports = (reportsData) => {
  const tags = {}
  reportsData.forEach(report => {
    report.cattle_tag_readings?.forEach(reading => {
      if (!tags[reading.tag_id] || 
          new Date(reading.created_at) > new Date(tags[reading.tag_id].created_at)) {
        tags[reading.tag_id] = reading
      }
    })
  })
  latestTags.value = tags
}

const updateLatestTags = (readings) => {
  readings.forEach(reading => {
    if (!latestTags.value[reading.tag_id] || 
        new Date(reading.created_at) > new Date(latestTags.value[reading.tag_id].created_at)) {
      latestTags.value[reading.tag_id] = reading
    }
  })
}

const formatDate = (dateString) => {
  return new Date(dateString).toLocaleString()
}
</script>
```

## Common Queries

### Get Tag Count Over Time

```javascript
// Count unique tags seen per hour
const { data, error } = await supabase
  .from('cattle_tag_readings')
  .select('created_at, tag_id')
  .gte('created_at', new Date(Date.now() - 24 * 60 * 60 * 1000).toISOString())

if (data) {
  // Group by hour and count unique tags
  const hourlyCounts = {}
  data.forEach(reading => {
    const hour = new Date(reading.created_at).toISOString().slice(0, 13) + ':00:00'
    if (!hourlyCounts[hour]) {
      hourlyCounts[hour] = new Set()
    }
    hourlyCounts[hour].add(reading.tag_id)
  })
  
  const result = Object.entries(hourlyCounts).map(([hour, tags]) => ({
    hour,
    count: tags.size
  }))
}
```

### Get Tags by Location (Node)

```javascript
// Get all unique tags seen by each node
const { data, error } = await supabase
  .from('cattle_presence_reports')
  .select(`
    from_node_hex,
    node_id,
    cattle_tag_readings (tag_id)
  `)

if (data) {
  const tagsByNode = {}
  data.forEach(report => {
    if (!tagsByNode[report.from_node_hex]) {
      tagsByNode[report.from_node_hex] = new Set()
    }
    report.cattle_tag_readings?.forEach(reading => {
      tagsByNode[report.from_node_hex].add(reading.tag_id)
    })
  })
}
```

### Get Tag Movement History

```javascript
// Track a tag's movement between nodes over time
const tagId = 4575

const { data, error } = await supabase
  .from('cattle_tag_readings')
  .select(`
    *,
    cattle_presence_reports!inner (
      from_node_hex,
      node_id,
      received_at
    )
  `)
  .eq('tag_id', tagId)
  .order('created_at', { ascending: true })

// Data will show the tag's movement between different nodes
```

## Error Handling

```javascript
// Always handle errors when querying Supabase
const { data, error } = await supabase
  .from('cattle_presence_reports')
  .select('*')

if (error) {
  // Handle different error types
  if (error.code === 'PGRST116') {
    console.error('No rows returned')
  } else if (error.code === '42501') {
    console.error('Permission denied - check RLS policies')
  } else {
    console.error('Supabase error:', error.message)
  }
  return
}

// Use data
console.log(data)
```

## Performance Tips

1. **Use Pagination**: Limit results to avoid loading too much data
   ```javascript
   .limit(50)
   .range(0, 49)
   ```

2. **Select Only Needed Columns**: Don't select `*` if you don't need all columns
   ```javascript
   .select('id, tag_id, rssi, created_at')
   ```

3. **Use Indexes**: The schema includes indexes on commonly queried columns (tag_id, received_at, etc.)

4. **Cache Results**: Cache frequently accessed data client-side

5. **Debounce Real-time Updates**: Don't update UI on every single insert if updates are frequent

## Security Notes

- The database uses Row Level Security (RLS) with public read access
- For production, you may want to restrict access based on authentication
- The `anon` key is safe to use client-side (it respects RLS policies)
- Never expose the `service_role` key in client-side code

## Testing

You can test your queries in the Supabase Dashboard:
1. Go to **SQL Editor**
2. Write test queries like:
   ```sql
   SELECT * FROM cattle_presence_reports 
   ORDER BY received_at DESC 
   LIMIT 10;
   ```
3. Run them to verify data structure

## Support

For issues or questions:
- Check Supabase logs in the Dashboard → Logs
- Verify real-time is enabled: Database → Replication
- Check RLS policies: Authentication → Policies

## Data Flow Summary

1. **Cattle tags** broadcast BLE advertisements
2. **Meshtastic nodes** scan for tags and create presence reports
3. **Gateway script** receives reports via mesh and stores to Supabase
4. **Your web app** queries Supabase and subscribes to real-time updates
5. **Users** see live cattle tracking data in the browser

The system reports cattle presence approximately every 3 minutes (configurable), so expect new data every few minutes when cattle are in range of tracking nodes.

