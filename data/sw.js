const WMBP_CACHE = 'wmb-plus-shell-v1';
const WMBP_SHELL = [
  '/app.html',
  '/manifest.webmanifest',
  '/output.css',
  '/css/all.min.css',
  '/js/alpine.min.js',
  '/favicon.png',
  '/wmb-plus-logo.jpg',
  '/icons/wmb-plus-192.png',
  '/icons/wmb-plus-512.png'
];

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(WMBP_CACHE)
      .then(cache => cache.addAll(WMBP_SHELL))
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

  if (request.method !== 'GET' || url.pathname.startsWith('/api/')) {
    return;
  }

  event.respondWith(
    fetch(request).catch(() => caches.match(request).then(response => response || caches.match('/app.html')))
  );
});
