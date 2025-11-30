-- Supabase Database Schema for Cattle Tracking System
-- Run this in your Supabase SQL Editor

-- Table: cattle_presence_reports
-- Stores each cattle presence report received from Meshtastic nodes
CREATE TABLE IF NOT EXISTS cattle_presence_reports (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    node_id INTEGER NOT NULL,
    from_node_hex TEXT NOT NULL,
    tag_count INTEGER NOT NULL,
    received_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Table: cattle_tag_readings
-- Stores individual tag readings from each report
CREATE TABLE IF NOT EXISTS cattle_tag_readings (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    report_id UUID NOT NULL REFERENCES cattle_presence_reports(id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL,
    rssi INTEGER NOT NULL,
    status INTEGER NOT NULL,
    distance_estimate TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Indexes for efficient querying
CREATE INDEX IF NOT EXISTS idx_reports_node_id ON cattle_presence_reports(node_id);
CREATE INDEX IF NOT EXISTS idx_reports_received_at ON cattle_presence_reports(received_at DESC);
CREATE INDEX IF NOT EXISTS idx_reports_from_node_hex ON cattle_presence_reports(from_node_hex);
CREATE INDEX IF NOT EXISTS idx_readings_report_id ON cattle_tag_readings(report_id);
CREATE INDEX IF NOT EXISTS idx_readings_tag_id ON cattle_tag_readings(tag_id);
CREATE INDEX IF NOT EXISTS idx_readings_created_at ON cattle_tag_readings(created_at DESC);

-- Enable Row Level Security (RLS)
ALTER TABLE cattle_presence_reports ENABLE ROW LEVEL SECURITY;
ALTER TABLE cattle_tag_readings ENABLE ROW LEVEL SECURITY;

-- RLS Policies: Allow public read access (adjust based on your security needs)
-- For production, you may want to restrict this based on authentication
CREATE POLICY "Allow public read access to reports"
    ON cattle_presence_reports
    FOR SELECT
    USING (true);

CREATE POLICY "Allow public read access to readings"
    ON cattle_tag_readings
    FOR SELECT
    USING (true);

-- Allow service role to insert (for the gateway script)
-- Note: The gateway should use the service_role key, not anon key
CREATE POLICY "Allow service role to insert reports"
    ON cattle_presence_reports
    FOR INSERT
    WITH CHECK (true);

CREATE POLICY "Allow service role to insert readings"
    ON cattle_tag_readings
    FOR INSERT
    WITH CHECK (true);

-- Enable real-time for both tables (required for web app subscriptions)
ALTER PUBLICATION supabase_realtime ADD TABLE cattle_presence_reports;
ALTER PUBLICATION supabase_realtime ADD TABLE cattle_tag_readings;

-- Optional: Create a view for recent tag activity
CREATE OR REPLACE VIEW recent_tag_activity AS
SELECT 
    ctr.tag_id,
    ctr.rssi,
    ctr.distance_estimate,
    cpr.from_node_hex,
    cpr.node_id,
    cpr.received_at,
    ctr.created_at
FROM cattle_tag_readings ctr
JOIN cattle_presence_reports cpr ON ctr.report_id = cpr.id
ORDER BY ctr.created_at DESC;

-- Grant permissions on the view
GRANT SELECT ON recent_tag_activity TO anon, authenticated;

