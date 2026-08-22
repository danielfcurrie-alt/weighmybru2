// Bump this cache name whenever WMBP_SHELL assets are added, removed, renamed,
// or meaningfully changed so installed PWAs refresh their offline shell.
const WMBP_CACHE = 'wmb-plus-shell-v15';
const WMBP_SHELL = [
  '/',
  '/index.html',
  '/app.html',
  '/pourover.html',
  '/pourover.js',
  '/offline.html',
  '/calibration.html',
  '/settings.html',
  '/updates.html',
  '/stopmybru.html',
  '/stopmybrew.html',
  '/smb.html',
  '/manifest.webmanifest',
  '/stopmybru.webmanifest',
  '/output.css',
  '/view-mode.js',
  '/live-client.js',
  '/css/all.min.css',
  '/js/alpine.min.js',
  '/webfonts/fa-solid-900.woff2',
  '/icons/wmb-plus-192.png',
  '/icons/wmb-plus-512.jpg',
  '/icons/wmb-plus-maskable-512.png'
];

function isNavigationRequest(request) {
  const accept = request.headers.get('accept') || '';
  return request.mode === 'navigate' || accept.includes('text/html');
}

function cacheResponse(request, response) {
  if (!response || !response.ok || response.type !== 'basic') {
    return response;
  }

  const copy = response.clone();
  caches.open(WMBP_CACHE)
    .then(cache => cache.put(request, copy))
    .catch(() => undefined);
  return response;
}

function offlineAssetResponse() {
  return new Response('', {
    status: 504,
    statusText: 'Offline'
  });
}

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(WMBP_CACHE)
      .then(cache => Promise.all(WMBP_SHELL.map(path => cache.add(path).catch(() => undefined))))
      .catch(() => undefined)
  );
  self.skipWaiting();
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys => Promise.all(
      keys.filter(key => key !== WMBP_CACHE).map(key => caches.delete(key))
    ))
  );
  self.clients.claim();
});

self.addEventListener('fetch', event => {
  const request = event.request;
  const url = new URL(request.url);

  if (request.method !== 'GET' || url.origin !== self.location.origin || url.pathname.startsWith('/api/')) {
    return;
  }

  if (isNavigationRequest(request)) {
    event.respondWith(
      fetch(request)
        .then(response => cacheResponse(request, response))
        .catch(() => caches.match(request)
          .then(response => response || caches.match('/offline.html'))
          .then(response => response || caches.match('/app.html'))
          .then(response => response || caches.match('/index.html')))
    );
    return;
  }

  event.respondWith(
    fetch(request)
      .then(response => cacheResponse(request, response))
      .catch(() => caches.match(request).then(response => response || offlineAssetResponse()))
  );
});
