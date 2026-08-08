const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const path = require('path');
const fs = require('fs');
const webpush = require('web-push');

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(bodyParser.json());

// Serve static frontend assets from public/ folder
app.use(express.static(path.join(__dirname, '../public')));

// Path for VAPID credentials caching to persist keys between restarts
const VAPID_FILE = path.join(__dirname, '.vapid.json');
let vapidKeys;

if (fs.existsSync(VAPID_FILE)) {
  try {
    vapidKeys = JSON.parse(fs.readFileSync(VAPID_FILE, 'utf8'));
  } catch (err) {
    console.error('Error reading VAPID file, regenerations will occur:', err);
  }
}

if (!vapidKeys) {
  vapidKeys = webpush.generateVAPIDKeys();
  try {
    fs.writeFileSync(VAPID_FILE, JSON.stringify(vapidKeys), 'utf8');
    console.log('Generated new VAPID keys and saved to .vapid.json');
  } catch (err) {
    console.error('Could not save VAPID keys:', err);
  }
}

webpush.setVapidDetails(
  'mailto:eldercare-alerts@domain.com',
  vapidKeys.publicKey,
  vapidKeys.privateKey
);

// Telemetry initial state
let telemetryState = {
  status: "NORMAL",
  impact: 0.0,
  lat: 12.9716,
  lon: 77.5946,
  hr: 0,
  battery: 89,
  spo2: 0,
  ax: 0.0,
  ay: 0.0,
  az: 1.0,
  deviceName: "ElderCare SafeBand v1",
  connectionStatus: "Connected",
  lastUpdated: new Date().toISOString()
};

// SSE and Push subscriptions setup
let sseClients = [];
let pushSubscriptions = [];

// Helper function to broadcast state to SSE clients
function broadcastState() {
  const data = JSON.stringify(telemetryState);
  sseClients.forEach(client => {
    client.res.write(`data: ${data}\n\n`);
  });
}

// Helper to trigger push notifications
async function triggerPushNotification(payload) {
  const notificationPayload = JSON.stringify({
    title: 'EMERGENCY: Fall Detected!',
    body: `Fall detected. Impact: ${payload.impact || 0.0}G. BPM: ${payload.hr || 0}. Click to view location.`,
    icon: '/icons/icon-192.png',
    badge: '/icons/icon-192.png',
    data: {
      url: '/index.html'
    }
  });

  const promises = pushSubscriptions.map(sub => {
    return webpush.sendNotification(sub, notificationPayload)
      .catch(err => {
        // Clear expired subscriptions (410 Gone / 404 Not Found)
        if (err.statusCode === 410 || err.statusCode === 404) {
          console.log('Removing old/expired push subscription');
          pushSubscriptions = pushSubscriptions.filter(s => s !== sub);
        } else {
          console.error('Error sending push notification:', err);
        }
      });
  });

  await Promise.all(promises);
}

// SSE Connection Endpoint for Live client updates
app.get('/api/events', (req, res) => {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive'
  });
  
  // Write current status instantly upon registration
  res.write(`data: ${JSON.stringify(telemetryState)}\n\n`);

  const clientIdx = Date.now();
  const newClient = { id: clientIdx, res };
  sseClients.push(newClient);

  req.on('close', () => {
    sseClients = sseClients.filter(client => client.id !== clientIdx);
  });
});

// Telemetry state querying endpoint
app.get('/api/status', (req, res) => {
  res.json(telemetryState);
});

// ESP32 alert ingestion webhook
app.post('/api/alert', async (req, res) => {
  const { status, impact, lat, lon, hr, battery, spo2, ax, ay, az } = req.body;
  console.log('Received ESP32 / Alert payload:', req.body);

  // Update backend memory state
  telemetryState = {
    status: status || telemetryState.status,
    impact: typeof impact === 'number' ? impact : telemetryState.impact,
    lat: typeof lat === 'number' ? lat : telemetryState.lat,
    lon: typeof lon === 'number' ? lon : telemetryState.lon,
    hr: typeof hr === 'number' ? hr : telemetryState.hr,
    battery: typeof battery === 'number' ? battery : telemetryState.battery,
    spo2: typeof spo2 === 'number' ? spo2 : telemetryState.spo2,
    ax: typeof ax === 'number' ? ax : telemetryState.ax,
    ay: typeof ay === 'number' ? ay : telemetryState.ay,
    az: typeof az === 'number' ? az : telemetryState.az,
    deviceName: telemetryState.deviceName,
    connectionStatus: "Connected",
    lastUpdated: new Date().toISOString()
  };

  // Broadcast backend updates to clients
  broadcastState();

  // If status is a fall, request push alert
  if (telemetryState.status === "FALL_DETECTED") {
    console.log('Fall detected. Triggering push notifications...');
    await triggerPushNotification(telemetryState);
  }

  res.status(200).json({ success: true, message: 'Telemetry updated successfully', state: telemetryState });
});

// Reset fall status endpoint (e.g. from Emergency Modal False Alarm action)
app.post('/api/clear', (req, res) => {
  console.log('Clearing emergency / fall status (user cancelled)...');
  
  telemetryState.status = "NORMAL";
  telemetryState.lastUpdated = new Date().toISOString();

  // Broadcast update
  broadcastState();

  res.status(200).json({ success: true, state: telemetryState });
});

// Retreive Public Web Push Key
app.get('/api/vapid-public-key', (req, res) => {
  res.json({ publicKey: vapidKeys.publicKey });
});

// Register Web Push subscription
app.post('/api/subscribe', (req, res) => {
  const subscription = req.body;
  
  // Basic validation
  if (!subscription || !subscription.endpoint) {
    return res.status(400).json({ error: 'Invalid web push subscription' });
  }

  // Avoid duplicates
  const alreadySubscribed = pushSubscriptions.some(sub => sub.endpoint === subscription.endpoint);
  if (!alreadySubscribed) {
    pushSubscriptions.push(subscription);
    console.log('New web push subscription registered:', subscription.endpoint.slice(0, 40) + '...');
  }

  res.status(201).json({ success: true });
});

// Device battery / state simulator controller endpoints (optional utility for testing)
app.post('/api/simulate-device', (req, res) => {
  const { connectionStatus, battery } = req.body;
  if (connectionStatus) telemetryState.connectionStatus = connectionStatus;
  if (typeof battery === 'number') telemetryState.battery = battery;
  telemetryState.lastUpdated = new Date().toISOString();
  broadcastState();
  res.json(telemetryState);
});

// Setup catch-all request and serve index.html
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, '../public/index.html'));
});

// Launch server instance
app.listen(PORT, () => {
  console.log(`ElderCare Fall Detection Server running on http://localhost:${PORT}`);
});
