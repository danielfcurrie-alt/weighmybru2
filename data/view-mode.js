(function () {
  const STORAGE_KEY = 'wmbp.viewMode';
  const MODES = ['basic', 'extended'];

  function normalizeMode(mode) {
    const value = String(mode || '').toLowerCase();
    if (value === 'extended' || value === 'advanced' || value === 'data') return 'extended';
    return 'basic';
  }

  function injectStyle() {
    if (document.getElementById('wmbp-view-mode-style')) return;
    const style = document.createElement('style');
    style.id = 'wmbp-view-mode-style';
    style.textContent = `
      html.wmbp-standalone body {
        overscroll-behavior-y: contain;
      }
      html.wmbp-standalone #installCard,
      html.wmbp-standalone #stopInstallCard,
      html.wmbp-standalone .wmbp-browser-only {
        display: none !important;
      }
      html[data-wmbp-view-mode="basic"] [data-wmbp-extended] {
        display: none !important;
      }
      html[data-wmbp-view-mode="extended"] [data-wmbp-basic] {
        display: none !important;
      }
      .wmbp-view-mode-control {
        display: inline-grid;
        grid-template-columns: repeat(2, minmax(0, 1fr));
        gap: 3px;
        width: min(100%, 240px);
        margin: 0 0 18px;
        padding: 3px;
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.08);
        border: 1px solid rgba(255, 255, 255, 0.10);
      }
      .wmbp-view-mode-control button {
        min-height: 34px;
        border: 0;
        border-radius: 6px;
        padding: 7px 10px;
        background: transparent;
        color: #cbd5e1;
        cursor: pointer;
        font: inherit;
        font-size: 0.86rem;
        font-weight: 760;
        white-space: nowrap;
      }
      .wmbp-view-mode-control button[aria-pressed="true"] {
        background: rgba(71, 168, 100, 0.92);
        color: #071116;
      }
      .wmbp-view-mode-control button:focus-visible {
        outline: 2px solid rgba(134, 239, 172, 0.9);
        outline-offset: 2px;
      }
      @media (max-width: 480px) {
        .wmbp-view-mode-control {
          width: 100%;
        }
      }
      @media (display-mode: standalone) {
        .wmbp-view-mode-control {
          margin-bottom: 14px;
        }
      }
    `;
    document.head.appendChild(style);
  }

  function isStandaloneMode() {
    return window.matchMedia('(display-mode: standalone)').matches || window.navigator.standalone === true;
  }

  function applyDisplayMode() {
    document.documentElement.classList.toggle('wmbp-standalone', isStandaloneMode());
  }

  function currentMode() {
    const queryMode = new URLSearchParams(window.location.search).get('view');
    if (queryMode) return normalizeMode(queryMode);
    return normalizeMode(window.localStorage?.getItem(STORAGE_KEY));
  }

  function updateButtons(mode) {
    document.querySelectorAll('[data-wmbp-view-mode-control]').forEach(control => {
      control.querySelectorAll('[data-wmbp-view-choice]').forEach(button => {
        const selected = normalizeMode(button.dataset.wmbpViewChoice) === mode;
        button.setAttribute('aria-pressed', selected ? 'true' : 'false');
        button.classList.toggle('tab-active', selected);
        button.classList.toggle('tab-idle', !selected);
      });
    });
  }

  function applyMode(mode) {
    const selected = normalizeMode(mode);
    try {
      window.localStorage?.setItem(STORAGE_KEY, selected);
    } catch (error) {
      // Storage can be unavailable in private browser modes.
    }
    document.documentElement.dataset.wmbpViewMode = selected;
    document.body?.setAttribute('data-wmbp-view-mode', selected);
    updateButtons(selected);
    window.dispatchEvent(new CustomEvent('wmbp:view-mode-change', { detail: { mode: selected } }));
  }

  function createControl() {
    const control = document.createElement('section');
    control.className = 'wmbp-view-mode-control';
    control.dataset.wmbpViewModeControl = '';
    control.setAttribute('aria-label', 'View mode');
    control.setAttribute('role', 'group');
    control.innerHTML = `
      <button type="button" data-wmbp-view-choice="basic">Basic</button>
      <button type="button" data-wmbp-view-choice="extended">Extended</button>
    `;
    return control;
  }

  function ensureControl() {
    if (document.querySelector('[data-wmbp-view-mode-control]')) return;
    const container = document.querySelector('main') ||
      document.querySelector('.dashboard-container-responsive') ||
      document.querySelector('[role="main"]') ||
      document.body;
    if (!container) return;
    const heading = container.querySelector('h1');
    const control = createControl();
    if (heading) {
      const topbar = heading.closest('.topbar');
      if (topbar && container.contains(topbar)) {
        topbar.insertAdjacentElement('afterend', control);
        return;
      }
      const parent = heading.parentElement;
      const parentClass = parent?.className || '';
      const parentIsHeaderRow = parent !== container && (
        String(parentClass).includes('flex') ||
        String(parentClass).includes('justify-') ||
        parent.children.length > 1
      );
      (parentIsHeaderRow ? parent : heading).insertAdjacentElement('afterend', control);
    } else {
      container.insertAdjacentElement('afterbegin', control);
    }
  }

  function wireControls() {
    document.querySelectorAll('[data-wmbp-view-choice]').forEach(button => {
      if (button.dataset.wmbpViewModeWired === 'true') return;
      button.dataset.wmbpViewModeWired = 'true';
      button.addEventListener('click', () => applyMode(button.dataset.wmbpViewChoice));
    });
    updateButtons(currentMode());
  }

  window.WMBPViewMode = {
    get: currentMode,
    set: applyMode,
    normalize: normalizeMode
  };

  injectStyle();
  applyDisplayMode();
  document.documentElement.dataset.wmbpViewMode = currentMode();

  document.addEventListener('DOMContentLoaded', () => {
    applyDisplayMode();
    ensureControl();
    wireControls();
    applyMode(currentMode());
  });

  try {
    window.matchMedia('(display-mode: standalone)').addEventListener('change', applyDisplayMode);
  } catch (error) {
    // Older browsers do not expose media query change events here.
  }
})();
