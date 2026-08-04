/* ----------------------------------------------------
   ElderCare SafeLink - Client Application JS
   Coordinating telemetry data, location maps, Web Push, and audio alerts.
---------------------------------------------------- */

// Web Audio API Context for generating synthetic warning sirens
let audioCtx = null;
let sirenInterval = null;

// Leaflet Map state variables
let map = null;
let marker = null;
const DEFAULT_COORDS = [12.9716, 77.5946]; // Bangalore standard coords

// Notifications System local variables
let notifications = [];
try {
  notifications = JSON.parse(localStorage.getItem('elder_notifications') || '[]');
} catch (e) {
  notifications = [];
}
let lastDetectedStatus = null;

document.addEventListener('DOMContentLoaded', () => {
  // Load saved patient name preference
  const savedName = localStorage.getItem('patient-name') || 'James Smith';
  const nameDisplay = document.getElementById('profile-patient-name');
  const nameInput = document.getElementById('patient-name-input');
  if (nameDisplay) nameDisplay.innerText = savedName;
  if (nameInput) nameInput.value = savedName;

  initCalendar();
  initMap();
  initTabs();
  initECGSparkline();
  registerPWAComponents();
  connectTelemetrySSE();
  setupActionListeners();
  renderNotifications();
});

/* ----------------------------------------------------
   CALENDAR STRIP GENERATOR
---------------------------------------------------- */
function initCalendar() {
  const calendarDays = document.getElementById('calendar-days');
  const daysOfWeek = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const today = new Date();
  
  // Clear container and inject sliding background backplate
  calendarDays.innerHTML = '<div class="calendar-slider-bg" id="calendar-slider-bg"></div>';

  let activeNode = null;

  // Generate Mon-Sun strip around current date (from 3 days ago to 3 days ahead)
  for (let i = -3; i <= 3; i++) {
    const targetDate = new Date();
    targetDate.setDate(today.getDate() + i);

    const dayName = daysOfWeek[targetDate.getDay()];
    const dayNum = targetDate.getDate();
    const isToday = i === 0;

    const dayDayNode = document.createElement('div');
    dayDayNode.className = `cal-day-node ${isToday ? 'active' : ''}`;
    
    // Inline function to update content based on active state and today's status
    const updateContent = (isActive) => {
      if (isActive && isToday) {
        dayDayNode.innerHTML = `
          <span class="day-label">${dayName}</span>
          <span class="day-today">Today</span>
          <span class="day-num">${dayNum}</span>
        `;
      } else {
        dayDayNode.innerHTML = `
          <span class="day-label">${dayName}</span>
          <span class="day-num">${dayNum}</span>
        `;
      }
    };

    updateContent(isToday);

    dayDayNode.addEventListener('click', () => {
      document.querySelectorAll('.cal-day-node').forEach(node => {
        node.classList.remove('active');
        if (node.updateContentFn) {
          node.updateContentFn(false);
        }
      });
      
      dayDayNode.classList.add('active');
      updateContent(true);
      updateCalendarSlider(dayDayNode);
    });

    // Reference callback directly on node
    dayDayNode.updateContentFn = updateContent;

    calendarDays.appendChild(dayDayNode);

    if (isToday) {
      activeNode = dayDayNode;
    }
  }

  // Position date backplate slider after DOM layout calculations
  if (activeNode) {
    setTimeout(() => updateCalendarSlider(activeNode), 100);
  }
  
  // Reposition slider background on window resize events to keep visual alignment
  window.addEventListener('resize', () => {
    const node = document.querySelector('.cal-day-node.active');
    if (node) updateCalendarSlider(node);
  });
}

function updateCalendarSlider(node) {
  const slider = document.getElementById('calendar-slider-bg');
  if (slider && node) {
    slider.style.transform = `translateX(${node.offsetLeft}px)`;
    slider.style.width = `${node.offsetWidth}px`;
    slider.style.height = `${node.offsetHeight}px`;
    slider.style.top = `${node.offsetTop}px`;
  }
}

/* ----------------------------------------------------
   LEAFLET GPS MAP INITIALIZATION & UPDATE
---------------------------------------------------- */
function initMap() {
  try {
    // Scaffold Leaflet map instance centered on DEFAULT_COORDS
    map = L.map('leaflet-map', {
      zoomControl: false,
      attributionControl: false
    }).setView(DEFAULT_COORDS, 15);

    // Standard OpenStreetMap Tile Layer
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19
    }).addTo(map);

    // Custom Icon for patient indicator (Dark Green Palette)
    const elderIcon = L.divIcon({
      className: 'custom-map-marker',
      html: `
        <div style="
          width: 24px;
          height: 24px;
          background-color: #0F4C3F;
          border: 3px solid #ffffff;
          border-radius: 50%;
          box-shadow: 0 4px 10px rgba(15, 76, 63, 0.4);
          position: relative;
        ">
          <div style="
            width: 8px;
            height: 8px;
            background-color: #2DCD73;
            border-radius: 50%;
            position: absolute;
            top: 5px;
            left: 5px;
          "></div>
        </div>
      `,
      iconSize: [24, 24],
      iconAnchor: [12, 12]
    });

    marker = L.marker(DEFAULT_COORDS, { icon: elderIcon }).addTo(map);
  } catch (error) {
    console.error('Failed to initialize Leaflet Map. Network or offline issue:', error);
  }
}

function updateMapCoords(lat, lon) {
  if (map && marker) {
    const latLng = [lat, lon];
    marker.setLatLng(latLng);
    map.panTo(latLng);
    document.getElementById('coords-text').innerText = `${lat.toFixed(4)}° N, ${lon.toFixed(4)}° E`;
  }
}

/* ----------------------------------------------------
   NAVIGATION TABS CONTROL WITH SLIDING TRANSITIONS
---------------------------------------------------- */
function initTabs() {
  const tabs = document.querySelectorAll('.tab-btn');
  const navItems = document.querySelectorAll('.nav-item');

  // Multi-tier navigation router bindings
  tabs.forEach(tab => {
    tab.addEventListener('click', () => {
      const target = tab.getAttribute('data-tab');
      switchTab(target);
      
      // Update equivalent navbar highlights
      navItems.forEach(item => {
        const navBind = item.getAttribute('data-nav');
        if ((target === 'dashboard' && navBind === 'home') || 
            (target === 'health-logs' && navBind === 'health')) {
          item.classList.add('active');
        } else {
          item.classList.remove('active');
        }
      });
    });
  });

  navItems.forEach(item => {
    item.addEventListener('click', () => {
      const navBind = item.getAttribute('data-nav');
      if (navBind === 'home') {
        switchTab('dashboard');
      } else if (navBind === 'health') {
        switchTab('health-logs');
      }
      
      navItems.forEach(n => n.classList.remove('active'));
      item.classList.add('active');
    });
  });
}

function switchTab(tabId) {
  const tabs = document.querySelectorAll('.tab-btn');
  const panels = document.querySelectorAll('.tab-panel');
  const slider = document.getElementById('tabs-content-slider');
  const sliderBg = document.getElementById('tabs-slider-bg');

  // Update tabs active state classes
  tabs.forEach(tab => {
    if (tab.getAttribute('data-tab') === tabId) {
      tab.classList.add('active');
    } else {
      tab.classList.remove('active');
    }
  });

  // Calculate sliding offsets and assign css transforms
  if (tabId === 'dashboard') {
    if (slider) slider.style.transform = 'translateX(0)';
    if (sliderBg) sliderBg.style.transform = 'translateX(0)';
  } else if (tabId === 'health-logs') {
    if (slider) slider.style.transform = 'translateX(-50%)';
    // Move the background plate behind Health Logs tab (offset 100%)
    if (sliderBg) sliderBg.style.transform = 'translateX(100%)';
  }

  // Toggle active class on panels for opacity fades
  panels.forEach(panel => {
    if (panel.id === `tab-${tabId}`) {
      panel.classList.add('active');
    } else {
      panel.classList.remove('active');
    }
  });
}

/* ----------------------------------------------------
   ANIMATED ECG HEART RATE SPARKLINE (Canvas)
---------------------------------------------------- */
let ecgAnimationId = null;
function initECGSparkline() {
  const canvas = document.getElementById('bpm-sparkline');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  
  // Set dimensions based on layout container bounding client rect
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);

  const width = rect.width;
  const height = rect.height;

  // ECG Animation points parameters
  let x = 0;
  const points = [];
  const maxPoints = Math.floor(width / 2);
  let pulseTimer = 0;

  // Setup loop
  function drawECG() {
    ctx.clearRect(0, 0, width, height);
    ctx.beginPath();
    ctx.strokeStyle = '#2DCD73'; // Teal outline matching theme
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';

    // Generate normal sinus rhythm ECG pulse structure
    pulseTimer++;
    let currentYVal = height / 2;
    const period = 50; // controls speed/frequency of pulses

    if (pulseTimer % period === 0) {
      // Begin ECG complexes (P wave, QRS Spike, T wave)
    }

    const index = pulseTimer % period;
    if (index >= 10 && index <= 13) {
      currentYVal -= 3; // P-wave
    } else if (index === 15) {
      currentYVal += 5; // Q-point
    } else if (index === 17) {
      currentYVal -= 25; // R-spike
    } else if (index === 19) {
      currentYVal += 20; // S-drop
    } else if (index >= 24 && index <= 29) {
      currentYVal -= 6; // T-wave
    }

    points.push(currentYVal);
    if (points.length > maxPoints) {
      points.shift();
    }

    // Render ECG continuous path
    ctx.beginPath();
    for (let i = 0; i < points.length; i++) {
      const drawX = (i / maxPoints) * width;
      if (i === 0) {
        ctx.moveTo(drawX, points[i]);
      } else {
        ctx.lineTo(drawX, points[i]);
      }
    }
    ctx.stroke();

    ecgAnimationId = requestAnimationFrame(drawECG);
  }

  drawECG();
}

/* ----------------------------------------------------
   REALTIME TELEMETRY LISTENER (SSE Streams)
---------------------------------------------------- */
function connectTelemetrySSE() {
  const currentHost = window.location.origin;
  const eventSource = new EventSource(`${currentHost}/api/events`);
  
  eventSource.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      console.log('SSE Stream telemetry update received:', data);
      renderTelemetry(data);
    } catch (err) {
      console.error('Error parsing SSE event data:', err);
    }
  };

  eventSource.onerror = (err) => {
    console.warn('SSE connection failed. Server might be restarting or client is connectionless. Attempting automatic fallback polling...');
    eventSource.close();
    
    // Start polling fallback if SSE disconnects
    setTimeout(startPollingFallback, 3000);
  };
}

function startPollingFallback() {
  console.log('Polling fallback initiated...');
  const poll = async () => {
    try {
      const res = await fetch('/api/status');
      const data = await res.json();
      renderTelemetry(data);
    } catch (e) {
      console.error('Polling tick failed:', e);
    }
  };
  
  poll();
  setInterval(poll, 4000);
}

function renderTelemetry(data) {
  // 1. Alert Pulse indicators styles
  const pulseInd = document.getElementById('telemetry-pulse');
  const alertCard = document.getElementById('card-alert-status');
  const alertText = document.getElementById('fall-status-text');
  
  if (data.status !== lastDetectedStatus) {
    if (lastDetectedStatus !== null) {
      if (data.status === 'FALL_DETECTED') {
        addNotification('🚨 Emergency: Fall detected!', 'alert-notif');
      } else {
        addNotification('🟢 Device Status: Restored to Safe & Secure', 'safe-notif');
      }
    }
    lastDetectedStatus = data.status;
  }
  
  if (data.status === 'FALL_DETECTED') {
    pulseInd.className = 'pulse-indicator status-red';
    alertCard.classList.add('alert-active');
    alertText.innerText = 'FALL DETECTED!';
    alertText.className = 'status-value-large red-text';
    
    // Activate Emergency Overlay Modal
    openEmergencyModal(data);
  } else {
    pulseInd.className = 'pulse-indicator status-green';
    alertCard.classList.remove('alert-active');
    alertText.innerText = 'Safe & Secure';
    alertText.className = 'status-value-large green-text';
    
    // Close Emergency Overlay Modal
    closeEmergencyModal();
  }

  // 2. Battery & Steps Diagnostics
  document.getElementById('device-status-text').innerText = data.connectionStatus || 'Connected';
  document.getElementById('telemetry-impact').innerText = `${(data.impact || 0.0).toFixed(1)} G`;
  document.getElementById('sync-time').innerText = new Date(data.lastUpdated).toLocaleTimeString();
  document.getElementById('bpm-val').innerText = data.hr || '72';
  document.getElementById('battery-value-text').innerText = `${data.battery || 89}%`;

  // 3. Leaflet Dynamic Map Marker Location
  if (typeof data.lat === 'number' && typeof data.lon === 'number') {
    updateMapCoords(data.lat, data.lon);
  }

  // Update heart rate description warning threshold
  const hrSub = document.getElementById('bpm-subtext');
  if (data.hr > 100) {
    hrSub.innerText = 'Heart rate is elevated (Alert Level)';
    hrSub.style.color = 'var(--danger-red)';
  } else if (data.hr < 55) {
    hrSub.innerText = 'Heart rate is low (Bradycardia risks)';
    hrSub.style.color = 'var(--danger-red)';
  } else {
    hrSub.innerText = 'Pulse is currently normal & stable';
    hrSub.style.color = 'var(--text-muted)';
  }
}

/* ----------------------------------------------------
   EMERGENCY OVERLAY CONTROLLER & AUDIO GENERATOR
---------------------------------------------------- */
function openEmergencyModal(data) {
  const modal = document.getElementById('emergency-modal');
  modal.style.display = 'flex';
  
  document.getElementById('modal-impact-val').innerText = `${(data.impact || 3.2).toFixed(1)} G`;
  document.getElementById('modal-bpm-val').innerText = `${data.hr || 88} BPM`;
  document.getElementById('alert-badge').style.display = 'flex';

  // Dispatch Log Incident Row into health-logs container
  logIncident(data);

  // Play Warning Alarm Audio loop
  startWarningSiren();
}

function closeEmergencyModal() {
  const modal = document.getElementById('emergency-modal');
  modal.style.display = 'none';
  document.getElementById('alert-badge').style.display = 'none';
  
  // Mute Alarm Audio
  stopWarningSiren();
}

// Log a fall telemetry alert entry into Health Logs
function logIncident(data) {
  const logsContainer = document.getElementById('logs-list-container');
  const emptyState = logsContainer.querySelector('.empty-state');
  if (emptyState) {
    emptyState.remove();
  }

  // Check if this particular timestamp row is already added to avoid replica logs
  const logId = `log-${new Date(data.lastUpdated).getTime()}`;
  if (document.getElementById(logId)) return;

  const logItem = document.createElement('div');
  logItem.className = 'log-item incident';
  logItem.id = logId;
  logItem.innerHTML = `
    <div class="log-info-left">
      <span class="log-title">Critical fall detected</span>
      <span class="log-sub">Impact force: ${(data.impact || 0.0).toFixed(1)}G | GPS status active</span>
    </div>
    <div class="log-meta-right">
      <span class="log-badge badge-alert">Alert Mode</span>
      <span class="log-time">${new Date(data.lastUpdated).toLocaleTimeString()}</span>
    </div>
  `;

  logsContainer.prepend(logItem);
}

// Synth alarm warning synthesizer (siren oscillator)
function startWarningSiren() {
  if (sirenInterval) return; // already active

  try {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    
    sirenInterval = setInterval(() => {
      if (!audioCtx) return;
      
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();

      osc.connect(gain);
      gain.connect(audioCtx.destination);

      osc.type = 'sine';
      // Alternate high pitches mimicking a warning alert siren sound
      const timeSecs = Date.now() / 1000;
      osc.frequency.setValueAtTime(450 + Math.sin(timeSecs * 8) * 150, audioCtx.currentTime);

      gain.gain.setValueAtTime(0.08, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.3);

      osc.start(audioCtx.currentTime);
      osc.stop(audioCtx.currentTime + 0.31);
    }, 350);
  } catch (err) {
    console.warn('Audio Context disabled or blocked by browser user security rules:', err);
  }
}

function stopWarningSiren() {
  if (sirenInterval) {
    clearInterval(sirenInterval);
    sirenInterval = null;
  }
  if (audioCtx) {
    audioCtx.close();
    audioCtx = null;
  }
}

/* ----------------------------------------------------
   SIMULATION ACTION EVENT LISTENERS
---------------------------------------------------- */
function setupActionListeners() {
  // Recenter Leaflet map viewport action
  document.getElementById('recenter-map').addEventListener('click', () => {
    if (map && marker) {
      map.setView(marker.getLatLng(), 15);
    }
  });

  // Client cancellation of Fall (False Alarm)
  document.getElementById('cancel-emergency-btn').addEventListener('click', async () => {
    try {
      const res = await fetch('/api/clear', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
      });
      const resData = await res.json();
      console.log('Incident cleared successfully:', resData);
      
      // Update UI log entry list mapping to "False Alarm (Cancelled)"
      updateLogIncidentToCancelled();
      closeEmergencyModal();
    } catch (e) {
      console.error('Failed to notify node on cleared fall status:', e);
      closeEmergencyModal(); // force local dismissal
    }
  });

  // Simulate Fall Alert trigger POST
  document.getElementById('sim-fall-btn').addEventListener('click', async () => {
    const payload = {
      status: "FALL_DETECTED",
      impact: 3.5,
      lat: 12.9780, // slightly shifted coords to mock movement
      lon: 77.5910,
      hr: 104,
      battery: 87
    };
    triggerSimulatedTelemetry(payload);
  });

  // Simulate Safe mode telemetry POST
  document.getElementById('sim-safe-btn').addEventListener('click', () => {
    const payload = {
      status: "NORMAL",
      impact: 0.1,
      lat: DEFAULT_COORDS[0],
      lon: DEFAULT_COORDS[1],
      hr: 74,
      battery: 91
    };
    triggerSimulatedTelemetry(payload);
  });

  // Floating Emergency icon shortcut button behavior (triggers an alert simulation directly)
  document.getElementById('emergency-shortcut-btn').addEventListener('click', () => {
    const payload = {
      status: "FALL_DETECTED",
      impact: 4.1,
      lat: 12.9690,
      lon: 77.5990,
      hr: 112,
      battery: 85
    };
    triggerSimulatedTelemetry(payload);
  });

  // Profile Drawer Event Wireups
  const avatarBtn = document.getElementById('avatar-profile-btn');
  const drawerOverlay = document.getElementById('profile-drawer-overlay');
  const drawerClose = document.getElementById('profile-drawer-close');
  const saveNameBtn = document.getElementById('save-patient-name-btn');
  const nameInput = document.getElementById('patient-name-input');
  const patientNameDisplay = document.getElementById('profile-patient-name');

  if (avatarBtn && drawerOverlay) {
    avatarBtn.addEventListener('click', () => {
      // Sync input field value before opening
      if (nameInput && patientNameDisplay) {
        nameInput.value = patientNameDisplay.innerText.trim();
      }
      drawerOverlay.classList.add('active');
    });
  }

  if (drawerClose && drawerOverlay) {
    drawerClose.addEventListener('click', () => {
      drawerOverlay.classList.remove('active');
    });
  }

  if (drawerOverlay) {
    drawerOverlay.addEventListener('click', (e) => {
      if (e.target === drawerOverlay) {
        drawerOverlay.classList.remove('active');
      }
    });
  }

  if (saveNameBtn && nameInput && patientNameDisplay && drawerOverlay) {
    saveNameBtn.addEventListener('click', () => {
      const newName = nameInput.value.trim() || 'Patient Name';
      patientNameDisplay.innerText = newName;
      localStorage.setItem('patient-name', newName);
      drawerOverlay.classList.remove('active');
    });
  }

  // Notification Bell Dropdown toggle listeners
  const bellBtn = document.getElementById('bell-btn');
  const notifDropdown = document.getElementById('notifications-dropdown');
  const clearNotifBtn = document.getElementById('clear-notifications-btn');

  if (bellBtn && notifDropdown) {
    bellBtn.addEventListener('click', (e) => {
      // Don't toggle dropdown if close button or clear button clicked
      if (e.target.closest('#clear-notifications-btn')) return;
      notifDropdown.classList.toggle('active');
      e.stopPropagation();
    });

    document.addEventListener('click', (e) => {
      if (!bellBtn.contains(e.target)) {
        notifDropdown.classList.remove('active');
      }
    });
  }

  if (clearNotifBtn) {
    clearNotifBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      clearNotifications();
    });
  }
}

async function triggerSimulatedTelemetry(payload) {
  try {
    const res = await fetch('/api/alert', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const result = await res.json();
    console.log('Simulated telemetry payload submitted:', result);
  } catch (err) {
    console.error('Call failed during hardware telemetry simulation dispatch:', err);
  }
}

function updateLogIncidentToCancelled() {
  const firstIncidentLog = document.querySelector('.log-item.incident');
  if (firstIncidentLog) {
    firstIncidentLog.className = 'log-item';
    const badge = firstIncidentLog.querySelector('.log-badge');
    badge.className = 'log-badge badge-clear';
    badge.innerText = 'Cancelled';
  }
}

/* ----------------------------------------------------
   PWA REGISTRATION & WEB PUSH SERVICES
---------------------------------------------------- */
function registerPWAComponents() {
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js')
      .then(reg => {
        console.log('Service Worker registered successfully. Scope:', reg.scope);
        configurePushSubscription(reg);
      })
      .catch(err => {
        console.error('Service Worker registration failed:', err);
      });
  }
}

async function configurePushSubscription(swReg) {
  try {
    // 1. Fetch public vapid key
    const vapidRes = await fetch('/api/vapid-public-key');
    const vapidData = await vapidRes.json();
    const applicationServerKey = urlB64ToUint8Array(vapidData.publicKey);

    // 2. Request permission first
    const permission = await Notification.requestPermission();
    if (permission !== 'granted') {
      console.warn('User has rejected web notifications permissions.');
      return;
    }

    // 3. Subscribe user to sync push actions
    let subscription = await swReg.pushManager.getSubscription();
    if (!subscription) {
      subscription = await swReg.pushManager.subscribe({
        userVisibleOnly: true,
        applicationServerKey: applicationServerKey
      });
    }

    // 4. Send subscription packet to the backend
    await fetch('/api/subscribe', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(subscription)
    });
    
    console.log('PWA Push Notification registration initialized successfully!');
  } catch (err) {
    console.error('Web Push registration pipeline error:', err);
  }
}

// Utility conversion tool mapping URL base64 hashes for application server keys
function urlB64ToUint8Array(base64String) {
  const padding = '='.repeat((4 - base64String.length % 4) % 4);
  const base64 = (base64String + padding)
    .replace(/\-/g, '+')
    .replace(/_/g, '/');

  const rawData = window.atob(base64);
  const outputArray = new Uint8Array(rawData.length);

  for (let i = 0; i < rawData.length; ++i) {
    outputArray[i] = rawData.charCodeAt(i);
  }
  return outputArray;
}

/* ----------------------------------------------------
   NOTIFICATION MANAGEMENT STATE HELPER FUNCTIONS
---------------------------------------------------- */
function saveNotifications() {
  localStorage.setItem('elder_notifications', JSON.stringify(notifications));
}

function addNotification(text, className = '') {
  const notif = {
    id: Date.now(),
    text: text,
    className: className,
    time: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
  };
  notifications.unshift(notif);
  if (notifications.length > 50) {
    notifications.pop();
  }
  saveNotifications();
  renderNotifications();
}

function renderNotifications() {
  const listEl = document.getElementById('notifications-list');
  const clearBtn = document.getElementById('clear-notifications-btn');
  const badgeEl = document.getElementById('alert-badge');

  if (!listEl) return;

  listEl.innerHTML = '';

  if (notifications.length === 0) {
    listEl.innerHTML = '<li class="empty-state">No notifications</li>';
    if (clearBtn) clearBtn.style.display = 'none';
    if (badgeEl) badgeEl.style.display = 'none';
  } else {
    notifications.forEach(notif => {
      const li = document.createElement('li');
      li.className = `notification-item ${notif.className || ''}`;
      li.innerHTML = `${notif.text} <span class="notif-time">${notif.time}</span>`;
      listEl.appendChild(li);
    });
    if (clearBtn) clearBtn.style.display = 'block';
    if (badgeEl) {
      badgeEl.innerText = notifications.length;
      badgeEl.style.display = 'flex';
    }
  }
}

function clearNotifications() {
  notifications = [];
  saveNotifications();
  renderNotifications();
}
