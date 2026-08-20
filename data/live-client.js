(function () {
  const tabId = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
  const channel = 'BroadcastChannel' in window ? new BroadcastChannel('wmbp-live-v1') : null;
  const subscribers = new Set();
  const pending = new Map();
  let leaderId = null;
  let leaderSeenAt = 0;
  let isLeader = false;
  let eventSource = null;
  let fallbackTimer = null;
  let heartbeatTimer = null;
  let reconnectTimer = null;
  let reconnectDelayMs = 500;
  let staleTimer = null;
  let lastSnapshotAt = 0;
  let offlinePublished = false;
  const FALLBACK_INTERVAL_MS = 1000;
  const SSE_RECONNECT_MAX_MS = 10000;
  const STALE_SNAPSHOT_MS = 6500;

  function visible() {
    return document.visibilityState !== 'hidden';
  }

  function post(message) {
    if (channel) channel.postMessage(Object.assign({ tabId }, message));
  }

  function notify(snapshot) {
    if (!snapshot) return;
    subscribers.forEach(callback => {
      try { callback(snapshot); } catch (_) { /* subscriber errors are isolated */ }
    });
  }

  function scheduleStaleCheck() {
    if (staleTimer) {
      clearTimeout(staleTimer);
      staleTimer = null;
    }
    if (!visible() || !lastSnapshotAt) return;
    staleTimer = setTimeout(() => {
      staleTimer = null;
      if (!visible() || !lastSnapshotAt) return;
      const staleMs = Date.now() - lastSnapshotAt;
      if (staleMs >= STALE_SNAPSHOT_MS) {
        publishOffline('stale', staleMs);
      } else {
        scheduleStaleCheck();
      }
    }, STALE_SNAPSHOT_MS + 150);
  }

  function noteOnlineSnapshot() {
    lastSnapshotAt = Date.now();
    offlinePublished = false;
    scheduleStaleCheck();
  }

  function publishOffline(reason, staleMs = Date.now() - lastSnapshotAt) {
    if (offlinePublished) return;
    offlinePublished = true;
    const snapshot = {
      __wmbpOffline: true,
      __wmbpReason: reason || 'offline',
      __wmbpStaleMs: Math.max(0, staleMs || 0)
    };
    notify(snapshot);
    post({ type: 'snapshot', snapshot });
  }

  function publish(snapshot) {
    if (!snapshot.__wmbpOffline) noteOnlineSnapshot();
    notify(snapshot);
    post({ type: 'snapshot', snapshot });
  }

  async function directJson(url) {
    const response = await fetch(url, { cache: 'no-store' });
    if (!response.ok) throw new Error(`${url} ${response.status}`);
    return response.json();
  }

  function stopLiveInput() {
    if (eventSource) {
      eventSource.close();
      eventSource = null;
    }
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    if (fallbackTimer) {
      clearInterval(fallbackTimer);
      fallbackTimer = null;
    }
    if (staleTimer) {
      clearTimeout(staleTimer);
      staleTimer = null;
    }
  }

  async function pollFallbackSnapshot() {
    if (!isLeader || !visible()) return;
    try {
      publish(await directJson('/api/dashboard'));
    } catch (_) {
      publishOffline('fetch_failed');
    }
  }

  function startFallback(intervalMs) {
    if (fallbackTimer) return;
    pollFallbackSnapshot();
    fallbackTimer = setInterval(pollFallbackSnapshot, intervalMs);
  }

  function scheduleSseReconnect() {
    if (reconnectTimer || !isLeader || !visible()) return;
    const delay = reconnectDelayMs;
    reconnectDelayMs = Math.min(SSE_RECONNECT_MAX_MS, Math.round(reconnectDelayMs * 1.7));
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      if (!isLeader || !visible()) return;
      if (eventSource) {
        eventSource.close();
        eventSource = null;
      }
      startLiveInput();
    }, delay);
  }

  function startLiveInput() {
    if (!isLeader || !visible()) return;
    if ('EventSource' in window && !eventSource) {
      eventSource = new EventSource('/api/live');
      eventSource.addEventListener('snapshot', event => {
        try {
          reconnectDelayMs = 500;
          if (fallbackTimer) {
            clearInterval(fallbackTimer);
            fallbackTimer = null;
          }
          publish(JSON.parse(event.data));
        } catch (_) { /* bad event ignored */ }
      });
      eventSource.onerror = () => {
        if (eventSource) {
          eventSource.close();
          eventSource = null;
        }
        if (!lastSnapshotAt || Date.now() - lastSnapshotAt >= STALE_SNAPSHOT_MS) {
          publishOffline('sse_error');
        }
        startFallback(FALLBACK_INTERVAL_MS);
        scheduleSseReconnect();
      };
      return;
    }
    startFallback(FALLBACK_INTERVAL_MS);
  }

  function becomeLeader() {
    if (isLeader || !visible()) return;
    isLeader = true;
    leaderId = tabId;
    leaderSeenAt = Date.now();
    post({ type: 'leader' });
    startLiveInput();
    if (!heartbeatTimer) {
      heartbeatTimer = setInterval(() => {
        if (!isLeader || !visible()) {
          resignLeader();
          return;
        }
        post({ type: 'leader' });
      }, 1000);
    }
  }

  function resignLeader() {
    if (!isLeader) return;
    isLeader = false;
    stopLiveInput();
    post({ type: 'resign' });
  }

  function scheduleElection(delay = 300) {
    setTimeout(() => {
      if (!visible()) return;
      if (!leaderId || Date.now() - leaderSeenAt > 2500) {
        becomeLeader();
      }
    }, delay);
  }

  async function fetchJson(url, options = {}) {
    const method = (options.method || 'GET').toUpperCase();
    if (!channel || method !== 'GET') return directJson(url);
    if (isLeader && visible()) return directJson(url);

    const id = `${tabId}-${Date.now()}-${Math.random().toString(36).slice(2)}`;
    post({ type: 'fetch', id, url });
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(async () => {
        pending.delete(id);
        if (visible() && (!leaderId || Date.now() - leaderSeenAt > 2500)) {
          try {
            becomeLeader();
            resolve(await directJson(url));
          } catch (error) {
            reject(error);
          }
        } else {
          reject(new Error(`live leader unavailable for ${url}`));
        }
      }, 1600);
      pending.set(id, { resolve, reject, timeout });
    });
  }

  if (channel) {
    channel.onmessage = async event => {
      const message = event.data || {};
      if (!message.type || message.tabId === tabId) return;

      if (message.type === 'leader') {
        leaderId = message.tabId;
        leaderSeenAt = Date.now();
        if (isLeader && message.tabId < tabId) resignLeader();
        return;
      }
      if (message.type === 'resign' && leaderId === message.tabId) {
        leaderId = null;
        scheduleElection();
        return;
      }
      if (message.type === 'snapshot') {
        if (message.snapshot && !message.snapshot.__wmbpOffline) noteOnlineSnapshot();
        notify(message.snapshot);
        return;
      }
      if (message.type === 'fetch' && isLeader && visible()) {
        try {
          post({ type: 'fetch-result', id: message.id, ok: true, data: await directJson(message.url) });
        } catch (error) {
          post({ type: 'fetch-result', id: message.id, ok: false, error: String(error && error.message || error) });
        }
        return;
      }
      if (message.type === 'fetch-result') {
        const request = pending.get(message.id);
        if (!request) return;
        clearTimeout(request.timeout);
        pending.delete(message.id);
        message.ok ? request.resolve(message.data) : request.reject(new Error(message.error || 'fetch failed'));
      }
    };
    post({ type: 'hello' });
    scheduleElection();
  } else {
    scheduleElection(0);
  }

  document.addEventListener('visibilitychange', () => {
    if (visible()) {
      if (lastSnapshotAt && Date.now() - lastSnapshotAt >= STALE_SNAPSHOT_MS) {
        publishOffline('stale');
      }
      scheduleStaleCheck();
      scheduleElection(50);
    } else {
      resignLeader();
    }
  });

  window.WMBPLive = {
    subscribe(callback) {
      subscribers.add(callback);
      startLiveInput();
      return () => subscribers.delete(callback);
    },
    fetchJson,
    isLeader: () => isLeader
  };
})();
