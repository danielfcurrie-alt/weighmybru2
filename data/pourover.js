(function () {
  'use strict';

  const RECIPES_KEY = 'wmbp.pourover.recipes.v1';
  const SELECTED_KEY = 'wmbp.pourover.selected.v1';
  const LAST_BREW_KEY = 'wmbp.pourover.lastBrew.v1';
  const TYPE_META = {
    pour: { label: 'Pour', symbol: '●', color: 'var(--pour)' },
    pause: { label: 'Pause', symbol: 'Ⅱ', color: 'var(--pause)' },
    agitate: { label: 'Agitate', symbol: '↻', color: 'var(--agitate)' },
    drawdown: { label: 'Drawdown', symbol: '▽', color: 'var(--drawdown)' }
  };

  let recipes = [];
  let selectedRecipeId = '';
  let preparedRecipe = null;
  let session = null;
  let lastSnapshot = {};
  let lastSnapshotAt = 0;
  let wakeLock = null;
  let renderTimer = null;
  let firmwareSessionAvailable = false;
  let lastFirmwareTransition = -1;
  let recordedFirmwareFinish = -1;
  let firmwareCommandPendingUntil = 0;

  const $ = id => document.getElementById(id);

  function makeId(prefix) {
    if (window.crypto && typeof window.crypto.randomUUID === 'function') {
      return `${prefix}-${window.crypto.randomUUID()}`;
    }
    return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
  }

  function clone(value) {
    return JSON.parse(JSON.stringify(value));
  }

  function finite(value, fallback = 0) {
    const number = Number(value);
    return Number.isFinite(number) ? number : fallback;
  }

  function clamp(value, lower, upper) {
    return Math.max(lower, Math.min(upper, value));
  }

  function escapeHtml(value) {
    return String(value == null ? '' : value)
      .replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;')
      .replaceAll("'", '&#039;');
  }

  function newStage(type, overrides = {}) {
    const defaults = {
      pour: { name: 'Pour', targetGrams: 100, durationSec: 25, flowMin: 3, flowMax: 6, autoAdvance: true },
      pause: { name: 'Pause', durationSec: 30, autoAdvance: true },
      agitate: { name: 'Swirl', durationSec: 3, autoAdvance: true },
      drawdown: { name: 'Drawdown', durationSec: 75, autoAdvance: true }
    };
    return Object.assign({ id: makeId('stage'), type }, defaults[type] || defaults.pause, overrides);
  }

  function defaultRecipes() {
    return [
      {
        id: makeId('recipe'),
        name: 'Simple Two Pour',
        stages: [
          newStage('pour', { name: 'Bloom', targetGrams: 50, durationSec: 10, flowMin: 3, flowMax: 6 }),
          newStage('pause', { name: 'Bloom pause', durationSec: 40 }),
          newStage('pour', { name: 'Main pour', targetGrams: 250, durationSec: 60, flowMin: 3, flowMax: 5 }),
          newStage('drawdown', { durationSec: 90 })
        ]
      },
      {
        id: makeId('recipe'),
        name: 'Three Pulse',
        stages: [
          newStage('pour', { name: 'Bloom', targetGrams: 50, durationSec: 10, flowMin: 3, flowMax: 6 }),
          newStage('pause', { name: 'Bloom pause', durationSec: 40 }),
          newStage('pour', { name: 'Pour 2', targetGrams: 100, durationSec: 25, flowMin: 3, flowMax: 5 }),
          newStage('pause', { durationSec: 20 }),
          newStage('pour', { name: 'Pour 3', targetGrams: 100, durationSec: 25, flowMin: 3, flowMax: 5 }),
          newStage('pause', { durationSec: 20 }),
          newStage('pour', { name: 'Pour 4', targetGrams: 100, durationSec: 25, flowMin: 3, flowMax: 5 }),
          newStage('drawdown', { durationSec: 75 })
        ]
      }
    ];
  }

  function normalizeStage(stage) {
    const type = TYPE_META[stage && stage.type] ? stage.type : 'pause';
    return newStage(type, {
      id: String(stage && stage.id || makeId('stage')),
      name: String(stage && stage.name || TYPE_META[type].label).slice(0, 42),
      targetGrams: clamp(finite(stage && stage.targetGrams, 100), 1, 2000),
      durationSec: clamp(finite(stage && stage.durationSec, 30), 0, 3600),
      flowMin: clamp(finite(stage && stage.flowMin, 0), 0, 30),
      flowMax: clamp(finite(stage && stage.flowMax, 6), 0, 30),
      autoAdvance: stage && stage.autoAdvance !== false
    });
  }

  function normalizeRecipe(recipe, index) {
    const stages = Array.isArray(recipe && recipe.stages) ? recipe.stages.map(normalizeStage) : [];
    return {
      id: String(recipe && recipe.id || makeId('recipe')),
      name: String(recipe && recipe.name || `Recipe ${index + 1}`).slice(0, 42),
      stages: stages.length ? stages : [newStage('pour'), newStage('drawdown')]
    };
  }

  function loadRecipes() {
    try {
      const stored = JSON.parse(localStorage.getItem(RECIPES_KEY) || 'null');
      recipes = Array.isArray(stored) && stored.length
        ? stored.map(normalizeRecipe)
        : defaultRecipes();
    } catch (_) {
      recipes = defaultRecipes();
    }
    selectedRecipeId = localStorage.getItem(SELECTED_KEY) || recipes[0].id;
    if (!recipes.some(recipe => recipe.id === selectedRecipeId)) selectedRecipeId = recipes[0].id;
    persistRecipes(false);
  }

  function persistRecipes(showSaved = true) {
    try {
      localStorage.setItem(RECIPES_KEY, JSON.stringify(recipes));
      localStorage.setItem(SELECTED_KEY, selectedRecipeId);
      if (showSaved) {
        $('saveState').textContent = 'Saved';
        window.setTimeout(() => { $('saveState').textContent = 'Saved on this device'; }, 900);
      }
    } catch (_) {
      $('saveState').textContent = 'Browser storage unavailable';
    }
  }

  function selectedRecipe() {
    return recipes.find(recipe => recipe.id === selectedRecipeId) || recipes[0];
  }

  function formatClock(seconds) {
    const safe = Math.max(0, Math.round(finite(seconds)));
    const minutes = Math.floor(safe / 60);
    const remainder = safe % 60;
    return `${minutes}:${String(remainder).padStart(2, '0')}`;
  }

  function formatGrams(value, decimals = 0) {
    return `${finite(value).toFixed(decimals)}g`;
  }

  function recipeTotals(recipe) {
    return {
      water: recipe.stages.reduce((sum, stage) => sum + (stage.type === 'pour' ? finite(stage.targetGrams) : 0), 0),
      seconds: recipe.stages.reduce((sum, stage) => sum + finite(stage.durationSec), 0)
    };
  }

  function renderRecipeSelect() {
    $('recipeSelect').innerHTML = recipes.map(recipe =>
      `<option value="${escapeHtml(recipe.id)}"${recipe.id === selectedRecipeId ? ' selected' : ''}>${escapeHtml(recipe.name)}</option>`
    ).join('');
    $('deleteRecipeButton').disabled = recipes.length <= 1;
  }

  function stageFields(stage) {
    if (stage.type === 'pour') {
      return `
        <div class="field"><label>ADDED WATER</label><div class="input-unit"><input class="number-control" type="number" min="1" max="2000" step="1" value="${finite(stage.targetGrams)}" data-field="targetGrams"><span>g</span></div></div>
        <div class="field"><label>PLANNED TIME</label><div class="input-unit"><input class="number-control" type="number" min="0" max="3600" step="1" value="${finite(stage.durationSec)}" data-field="durationSec"><span>s</span></div></div>
        <div class="field"><label>FLOW MIN</label><div class="input-unit"><input class="number-control" type="number" min="0" max="30" step="0.1" value="${finite(stage.flowMin).toFixed(1)}" data-field="flowMin"><span>g/s</span></div></div>
        <div class="field"><label>FLOW MAX</label><div class="input-unit"><input class="number-control" type="number" min="0" max="30" step="0.1" value="${finite(stage.flowMax).toFixed(1)}" data-field="flowMax"><span>g/s</span></div></div>
        ${autoAdvanceField(stage, 'Advance at target weight')}`;
    }
    const labels = {
      pause: 'Advance when pause ends',
      agitate: 'Advance when agitation ends',
      drawdown: 'Advance when drawdown timer ends'
    };
    return `
      <div class="field"><label>DURATION</label><div class="input-unit"><input class="number-control" type="number" min="0" max="3600" step="1" value="${finite(stage.durationSec)}" data-field="durationSec"><span>s</span></div></div>
      ${autoAdvanceField(stage, labels[stage.type])}`;
  }

  function autoAdvanceField(stage, label) {
    return `<div class="check-row"><label>${escapeHtml(label)}</label><label class="switch"><input type="checkbox" data-field="autoAdvance"${stage.autoAdvance ? ' checked' : ''}><span></span></label></div>`;
  }

  function renderBuilder() {
    const recipe = selectedRecipe();
    renderRecipeSelect();
    $('recipeName').value = recipe.name;
    const totals = recipeTotals(recipe);
    $('summaryWater').textContent = formatGrams(totals.water);
    $('summaryStages').textContent = String(recipe.stages.length);
    $('summaryTime').textContent = formatClock(totals.seconds);
    $('stageList').innerHTML = recipe.stages.map((stage, index) => {
      const meta = TYPE_META[stage.type];
      return `<article class="stage-card" data-stage-id="${escapeHtml(stage.id)}" style="--stage-color: ${meta.color};">
        <div class="stage-accent"></div>
        <div class="stage-body">
          <div class="stage-head">
            <div class="stage-index"><span class="symbol" aria-hidden="true">${meta.symbol}</span></div>
            <input class="control stage-name" type="text" maxlength="42" value="${escapeHtml(stage.name)}" data-field="name" aria-label="Stage ${index + 1} name">
            <div class="stage-tools">
              <button class="icon-button" type="button" data-stage-action="up" title="Move up" aria-label="Move stage up"${index === 0 ? ' disabled' : ''}><span class="symbol">↑</span></button>
              <button class="icon-button" type="button" data-stage-action="down" title="Move down" aria-label="Move stage down"${index === recipe.stages.length - 1 ? ' disabled' : ''}><span class="symbol">↓</span></button>
              <button class="icon-button danger" type="button" data-stage-action="delete" title="Delete stage" aria-label="Delete stage"><span class="symbol">×</span></button>
            </div>
          </div>
          <div class="fields">${stageFields(stage)}</div>
        </div>
      </article>`;
    }).join('');
  }

  function updateRecipeName(value) {
    const recipe = selectedRecipe();
    recipe.name = String(value || 'Untitled Recipe').slice(0, 42);
    renderRecipeSelect();
    persistRecipes();
  }

  function updateStage(stageId, field, target) {
    const stage = selectedRecipe().stages.find(item => item.id === stageId);
    if (!stage) return;
    if (field === 'name') stage.name = String(target.value || TYPE_META[stage.type].label).slice(0, 42);
    else if (field === 'autoAdvance') stage.autoAdvance = Boolean(target.checked);
    else stage[field] = finite(target.value);
    persistRecipes();
    const totals = recipeTotals(selectedRecipe());
    $('summaryWater').textContent = formatGrams(totals.water);
    $('summaryTime').textContent = formatClock(totals.seconds);
  }

  function addStage(type) {
    selectedRecipe().stages.push(newStage(type));
    persistRecipes();
    renderBuilder();
    const cards = $('stageList').querySelectorAll('.stage-card');
    cards[cards.length - 1]?.scrollIntoView({ behavior: 'smooth', block: 'center' });
  }

  function stageAction(stageId, action) {
    const stages = selectedRecipe().stages;
    const index = stages.findIndex(stage => stage.id === stageId);
    if (index < 0) return;
    if (action === 'delete') stages.splice(index, 1);
    if (action === 'up' && index > 0) [stages[index - 1], stages[index]] = [stages[index], stages[index - 1]];
    if (action === 'down' && index < stages.length - 1) [stages[index + 1], stages[index]] = [stages[index], stages[index + 1]];
    if (!stages.length) stages.push(newStage('pour'));
    persistRecipes();
    renderBuilder();
  }

  function newRecipe() {
    const recipe = {
      id: makeId('recipe'),
      name: 'New Pour Over',
      stages: [newStage('pour', { name: 'Bloom', targetGrams: 50 }), newStage('pause', { durationSec: 40 }), newStage('pour', { name: 'Main pour', targetGrams: 250 }), newStage('drawdown')]
    };
    recipes.push(recipe);
    selectedRecipeId = recipe.id;
    persistRecipes();
    renderBuilder();
    $('recipeName').focus();
    $('recipeName').select();
  }

  function duplicateRecipe() {
    const copy = clone(selectedRecipe());
    copy.id = makeId('recipe');
    copy.name = `${copy.name} Copy`.slice(0, 42);
    copy.stages = copy.stages.map(stage => Object.assign({}, stage, { id: makeId('stage') }));
    recipes.push(copy);
    selectedRecipeId = copy.id;
    persistRecipes();
    renderBuilder();
  }

  function deleteRecipe() {
    if (recipes.length <= 1) return;
    const index = recipes.findIndex(recipe => recipe.id === selectedRecipeId);
    recipes.splice(index, 1);
    selectedRecipeId = recipes[Math.max(0, index - 1)].id;
    persistRecipes();
    renderBuilder();
  }

  function setView(view) {
    const builder = view === 'builder';
    $('builderView').hidden = !builder;
    $('brewView').hidden = builder;
    $('builderTab').classList.toggle('active', builder);
    $('brewTab').classList.toggle('active', !builder);
    $('builderTab').setAttribute('aria-selected', String(builder));
    $('brewTab').setAttribute('aria-selected', String(!builder));
    if (!builder) ensurePrepared();
  }

  function ensurePrepared() {
    if (!preparedRecipe) preparedRecipe = clone(selectedRecipe());
    renderLive();
  }

  function recipeFormBody(recipe) {
    const body = new URLSearchParams();
    body.set('name', recipe.name);
    body.set('stageCount', String(recipe.stages.length));
    recipe.stages.forEach((stage, index) => {
      const prefix = `s${index}`;
      body.set(`${prefix}Type`, stage.type);
      body.set(`${prefix}Name`, stage.name);
      body.set(`${prefix}TargetGrams`, String(finite(stage.targetGrams)));
      body.set(`${prefix}DurationSec`, String(finite(stage.durationSec)));
      body.set(`${prefix}FlowMin`, String(finite(stage.flowMin)));
      body.set(`${prefix}FlowMax`, String(finite(stage.flowMax)));
      body.set(`${prefix}AutoAdvance`, String(stage.autoAdvance !== false));
    });
    return body;
  }

  async function postPourOver(action, body) {
    if (action !== 'recipe') firmwareCommandPendingUntil = Date.now() + 1000;
    const response = await fetch(`/api/pourover/${action}`, {
      method: 'POST',
      body,
      headers: body ? { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' } : undefined
    });
    if (!response.ok) throw new Error(`pour-over ${action} ${response.status}`);
    return response;
  }

  async function prepareSelectedRecipe() {
    preparedRecipe = clone(selectedRecipe());
    session = null;
    setView('brew');
    $('brewMessage').textContent = 'Preparing recipe…';
    try {
      await postPourOver('recipe', recipeFormBody(preparedRecipe));
      firmwareSessionAvailable = true;
      $('brewMessage').textContent = 'Recipe ready on scale and phone.';
    } catch (_) {
      firmwareSessionAvailable = false;
      $('brewMessage').textContent = 'Recipe ready in this browser.';
    }
  }

  function currentStage() {
    if (!preparedRecipe || !session) return null;
    return preparedRecipe.stages[session.currentIndex] || null;
  }

  function stageElapsedMs(now = Date.now()) {
    if (!session) return 0;
    const end = session.status === 'paused' ? session.pauseStartedAt : now;
    return Math.max(0, end - session.stageStartedAt - session.stagePausedMs);
  }

  function totalElapsedMs(now = Date.now()) {
    if (!session) return 0;
    const end = session.status === 'paused' ? session.pauseStartedAt : (session.finishedAt || now);
    return Math.max(0, end - session.startedAt - session.totalPausedMs);
  }

  function beginSession() {
    ensurePrepared();
    const now = Date.now();
    session = {
      status: 'running',
      currentIndex: 0,
      startedAt: now,
      stageStartedAt: now,
      stagePausedMs: 0,
      totalPausedMs: 0,
      pauseStartedAt: 0,
      stageBaselineWeight: finite(lastSnapshot.weight),
      flowSum: 0,
      flowSamples: 0,
      peakFlow: 0,
      samples: [],
      lastRecordedAt: 0,
      agitationHeld: false,
      frozenDisplay: null,
      referenceSamples: loadReferenceSamples(preparedRecipe.id),
      results: new Array(preparedRecipe.stages.length).fill(null),
      finishedAt: 0
    };
    session.firmware = firmwareSessionAvailable;
    if (firmwareSessionAvailable) {
      postPourOver('start').catch(() => {
        session.firmware = false;
        firmwareSessionAvailable = false;
        $('brewMessage').textContent = 'Scale sync unavailable; brew continues on phone.';
      });
    }
    $('brewMessage').textContent = firmwareSessionAvailable ? 'Brew started on scale and phone.' : 'Brew started.';
    vibrate(30);
    renderLive();
  }

  function toggleStartPause() {
    if (!session || session.status === 'finished') {
      beginSession();
      return;
    }
    const now = Date.now();
    if (session.status === 'running') {
      session.status = 'paused';
      session.pauseStartedAt = now;
      $('brewMessage').textContent = 'Brew paused.';
      if (session.firmware) postPourOver('pause').catch(showFirmwareControlError);
    } else {
      const pausedFor = now - session.pauseStartedAt;
      session.stagePausedMs += pausedFor;
      session.totalPausedMs += pausedFor;
      session.pauseStartedAt = 0;
      session.status = 'running';
      $('brewMessage').textContent = 'Brew resumed.';
      if (session.firmware) postPourOver('resume').catch(showFirmwareControlError);
    }
    renderLive();
  }

  function captureStageResult(index) {
    const stage = preparedRecipe.stages[index];
    if (!stage || !session) return;
    const elapsedSeconds = stageElapsedMs() / 1000;
    const currentWeight = finite(lastSnapshot.weight, session.stageBaselineWeight);
    session.results[index] = {
      type: stage.type,
      name: stage.name,
      targetGrams: stage.type === 'pour' ? finite(stage.targetGrams) : null,
      actualAddedGrams: stage.type === 'pour' ? Math.max(0, currentWeight - session.stageBaselineWeight) : null,
      durationSeconds: elapsedSeconds,
      averageFlow: session.flowSamples ? session.flowSum / session.flowSamples : 0,
      completedAtSeconds: totalElapsedMs() / 1000
    };
  }

  function resetStageCounters() {
    session.stageStartedAt = Date.now();
    session.stagePausedMs = 0;
    session.pauseStartedAt = 0;
    session.stageBaselineWeight = finite(lastSnapshot.weight);
    session.flowSum = 0;
    session.flowSamples = 0;
  }

  function nextStage(automatic = false) {
    if (!session || session.status === 'finished') return;
    if (session.firmware) {
      if (!automatic) postPourOver('next').catch(showFirmwareControlError);
      return;
    }
    captureStageResult(session.currentIndex);
    if (session.currentIndex >= preparedRecipe.stages.length - 1) {
      finishSession();
      return;
    }
    session.currentIndex += 1;
    resetStageCounters();
    session.status = 'running';
    $('brewMessage').textContent = automatic ? 'Stage target reached.' : 'Advanced to next stage.';
    vibrate(automatic ? [25, 45, 25] : 25);
    renderLive();
  }

  function previousStage() {
    if (!session || session.currentIndex <= 0 || session.status === 'finished') return;
    if (session.firmware) {
      postPourOver('previous').catch(showFirmwareControlError);
      return;
    }
    session.results[session.currentIndex] = null;
    session.currentIndex -= 1;
    session.results[session.currentIndex] = null;
    resetStageCounters();
    session.status = 'running';
    $('brewMessage').textContent = 'Returned to previous stage.';
    renderLive();
  }

  function finishSession() {
    if (!session || session.status === 'finished') return;
    if (session.firmware) {
      postPourOver('finish').catch(showFirmwareControlError);
      return;
    }
    if (!session.results[session.currentIndex]) captureStageResult(session.currentIndex);
    session.status = 'finished';
    session.finishedAt = Date.now();
    const record = {
      recipe: preparedRecipe,
      results: session.results,
      samples: session.samples,
      durationSeconds: totalElapsedMs() / 1000,
      finishedAt: new Date().toISOString()
    };
    try { localStorage.setItem(LAST_BREW_KEY, JSON.stringify(record)); } catch (_) { /* best effort */ }
    $('brewMessage').textContent = 'Brew complete.';
    vibrate([40, 50, 40]);
    renderLive();
  }

  function showFirmwareControlError() {
    $('brewMessage').textContent = 'Scale did not accept that control. Waiting for live state.';
    vibrate([50, 40, 50]);
  }

  function loadReferenceSamples(recipeId) {
    try {
      const record = JSON.parse(localStorage.getItem(LAST_BREW_KEY) || 'null');
      return record && record.recipe && record.recipe.id === recipeId && Array.isArray(record.samples)
        ? record.samples
        : [];
    } catch (_) {
      return [];
    }
  }

  function flowColor(flow) {
    if (flow < 0.5) return 'var(--muted)';
    if (flow < 1.5) return 'var(--pour)';
    if (flow < 4) return 'var(--accent)';
    if (flow < 6) return 'var(--pause)';
    return 'var(--danger)';
  }

  function recordSample(snapshot) {
    if (!session || session.status !== 'running') return;
    const now = Date.now();
    if (now - session.lastRecordedAt < 100) return;
    session.lastRecordedAt = now;
    const frozen = session.agitationHeld && session.frozenDisplay;
    session.samples.push({
      t: Math.max(0, (now - session.startedAt - session.totalPausedMs) / 1000),
      weight: finite(frozen ? session.frozenDisplay.weight : snapshot.weight),
      flow: finite(frozen ? 0 : snapshot.flowrate),
      rawWeight: finite(snapshot.weight),
      rawFlow: finite(snapshot.flowrate),
      stage: session.currentIndex,
      agitation: session.agitationHeld
    });
  }

  function drawSeries(context, samples, xFor, yFor, valueKey, color, dashed = false) {
    if (!samples.length) return;
    context.save();
    context.strokeStyle = color;
    context.lineWidth = 2;
    context.lineJoin = 'round';
    context.lineCap = 'round';
    if (dashed) context.setLineDash([6, 6]);
    context.beginPath();
    samples.forEach((sample, index) => {
      const x = xFor(sample.t);
      const y = yFor(finite(sample[valueKey]));
      if (index === 0) context.moveTo(x, y);
      else context.lineTo(x, y);
    });
    context.stroke();
    context.restore();
  }

  function drawBrewChart() {
    const canvas = $('brewChart');
    if (!canvas) return;
    const current = session ? session.samples : [];
    const reference = session ? session.referenceSamples : loadReferenceSamples(preparedRecipe.id);
    $('chartEmpty').hidden = current.length > 1 || reference.length > 1;
    const rect = canvas.getBoundingClientRect();
    if (rect.width < 20 || rect.height < 20) return;
    const scale = Math.min(2, window.devicePixelRatio || 1);
    const width = Math.round(rect.width * scale);
    const height = Math.round(rect.height * scale);
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    const context = canvas.getContext('2d');
    context.setTransform(scale, 0, 0, scale, 0, 0);
    context.clearRect(0, 0, rect.width, rect.height);
    const pad = { left: 36, right: 30, top: 16, bottom: 24 };
    const plotWidth = rect.width - pad.left - pad.right;
    const plotHeight = rect.height - pad.top - pad.bottom;
    const all = current.concat(reference);
    const maxTime = Math.max(30, ...all.map(sample => finite(sample.t)));
    const maxWeight = Math.max(50, ...all.map(sample => finite(sample.weight))) * 1.08;
    const maxFlow = Math.max(6, ...current.map(sample => Math.max(0, finite(sample.flow)))) * 1.15;
    const xFor = value => pad.left + clamp(value / maxTime, 0, 1) * plotWidth;
    const weightY = value => pad.top + (1 - clamp(value / maxWeight, 0, 1)) * plotHeight;
    const flowY = value => pad.top + (1 - clamp(value / maxFlow, 0, 1)) * plotHeight;

    context.strokeStyle = 'rgba(255,255,255,0.08)';
    context.fillStyle = '#82908a';
    context.font = '10px -apple-system, sans-serif';
    context.textBaseline = 'middle';
    for (let row = 0; row <= 4; row += 1) {
      const y = pad.top + (plotHeight * row / 4);
      context.beginPath(); context.moveTo(pad.left, y); context.lineTo(rect.width - pad.right, y); context.stroke();
      context.fillText(`${Math.round(maxWeight * (1 - row / 4))}g`, 5, y);
    }
    context.textBaseline = 'top';
    context.fillText('0:00', pad.left, rect.height - 18);
    context.fillText(formatClock(maxTime), rect.width - pad.right - 26, rect.height - 18);
    drawSeries(context, reference, xFor, weightY, 'weight', '#7f8a86', true);
    drawSeries(context, current, xFor, weightY, 'weight', '#43a8e6');
    drawSeries(context, current, xFor, flowY, 'flow', '#4fd178');
  }

  function stageProgress(stage) {
    if (!stage || !session) return { ratio: 0, current: '—', target: '—', metricLabel: 'STAGE' };
    const elapsed = stageElapsedMs() / 1000;
    if (stage.type === 'pour') {
      const added = Math.max(0, finite(lastSnapshot.weight) - session.stageBaselineWeight);
      const target = Math.max(1, finite(stage.targetGrams));
      return {
        ratio: clamp(added / target, 0, 1),
        current: formatGrams(added, 1),
        target: `Target +${formatGrams(target)}`,
        detail: `${formatGrams(added, 1)} of ${formatGrams(target)}`,
        metricLabel: 'ADDED'
      };
    }
    const duration = Math.max(1, finite(stage.durationSec));
    return {
      ratio: clamp(elapsed / duration, 0, 1),
      current: formatClock(elapsed),
      target: `${formatClock(duration)} target`,
      detail: `${formatClock(elapsed)} of ${formatClock(duration)}`,
      metricLabel: 'STAGE'
    };
  }

  function stageTargetDetail(stage) {
    if (stage.type === 'pour') {
      return `+${formatGrams(stage.targetGrams)} · ${finite(stage.flowMin).toFixed(1)}–${finite(stage.flowMax).toFixed(1)}g/s`;
    }
    return `${formatClock(stage.durationSec)} planned`;
  }

  function resultDetail(result) {
    if (!result) return '';
    if (result.type === 'pour') {
      return `+${formatGrams(result.actualAddedGrams, 1)} · ${formatClock(result.durationSeconds)} · ${finite(result.averageFlow).toFixed(1)}g/s`;
    }
    return formatClock(result.durationSeconds);
  }

  function compactDuration(seconds) {
    const rounded = Math.max(0, Math.round(finite(seconds)));
    return rounded < 60 ? `${rounded}s` : formatClock(rounded);
  }

  function renderTimeline() {
    if (!preparedRecipe) {
      $('timeline').innerHTML = '<div class="empty">Select a recipe to begin.</div>';
      return;
    }
    let pourNumber = 0;
    let timelineWeight = 0;
    let plannedElapsed = 0;
    $('timeline').innerHTML = preparedRecipe.stages.map((stage, index) => {
      const meta = TYPE_META[stage.type];
      const result = session && session.results[index];
      const current = session && index === session.currentIndex && session.status !== 'finished';
      const complete = Boolean(result);
      const pending = Boolean(session && !complete && !current);
      plannedElapsed += finite(stage.durationSec);
      let band;
      if (stage.type === 'pour') {
        pourNumber += 1;
        const liveAdded = current ? Math.max(0, finite(lastSnapshot.weight) - session.stageBaselineWeight) : 0;
        const shownGrams = complete ? finite(result.actualAddedGrams) : (current ? liveAdded : finite(stage.targetGrams));
        const shownSeconds = complete ? finite(result.durationSeconds) : (current ? stageElapsedMs() / 1000 : finite(stage.durationSec));
        const liveFlow = Math.max(0, finite(lastSnapshot.flowrate));
        const shownFlow = complete ? finite(result.averageFlow) : (current ? liveFlow : (finite(stage.flowMin) + finite(stage.flowMax)) / 2);
        let railWeight;
        if (complete) {
          timelineWeight += finite(result.actualAddedGrams);
          railWeight = timelineWeight;
        } else if (current) {
          railWeight = timelineWeight + liveAdded;
          timelineWeight += finite(stage.targetGrams);
        } else {
          timelineWeight += finite(stage.targetGrams);
          railWeight = timelineWeight;
        }
        band = `<div class="timeline-band pour">
          <div class="timeline-cell timeline-mark" title="${escapeHtml(stage.name)}"><span class="timeline-symbol">● ${pourNumber}</span></div>
          <div class="timeline-cell">${escapeHtml(formatGrams(shownGrams, complete || current ? 1 : 0))}</div>
          <div class="timeline-cell">${escapeHtml(compactDuration(shownSeconds))}</div>
          <div class="timeline-cell">${escapeHtml(shownFlow.toFixed(1))}g/s</div>
        </div>
        <div class="timeline-rail"><span>${escapeHtml(formatGrams(railWeight, complete || current ? 1 : 0))}</span><span class="timeline-rail-line"></span><span>${escapeHtml(formatClock(complete ? result.completedAtSeconds : (current ? totalElapsedMs() / 1000 : plannedElapsed)))}</span></div>`;
      } else {
        const shownSeconds = complete ? finite(result.durationSeconds) : (current ? stageElapsedMs() / 1000 : finite(stage.durationSec));
        const railTime = complete ? result.completedAtSeconds : (current ? totalElapsedMs() / 1000 : plannedElapsed);
        band = `<div class="timeline-band timed${stage.type === 'agitate' ? ' agitate' : ''}">
          <div class="timed-center"><span class="timeline-symbol">${stage.type === 'agitate' ? '↻' : '◷'}</span><span>${escapeHtml(stage.name)} · ${escapeHtml(compactDuration(shownSeconds))}</span></div>
          ${current ? '<span class="timeline-live-tag">LIVE</span>' : ''}
        </div>
        <div class="timeline-rail"><span>${escapeHtml(formatGrams(timelineWeight, complete || current ? 1 : 0))}</span><span class="timeline-rail-line"></span><span>${escapeHtml(formatClock(railTime))}</span></div>`;
      }
      return `<div class="timeline-row${current ? ' current' : ''}${complete ? ' complete' : ''}${pending ? ' pending' : ''}">${band}</div>`;
    }).join('');
  }

  function renderLive() {
    ensurePreparedRecipeOnly();
    const recipe = preparedRecipe;
    const totals = recipeTotals(recipe);
    $('liveRecipeSummary').textContent = `${recipe.name} · ${formatGrams(totals.water)} · ${recipe.stages.length} stages`;
    const displaySnapshot = session && session.agitationHeld && session.frozenDisplay ? session.frozenDisplay : lastSnapshot;
    $('liveWeight').textContent = Number.isFinite(Number(displaySnapshot.weight)) ? formatGrams(displaySnapshot.weight, 1) : '—';
    const flow = finite(displaySnapshot.flowrate, NaN);
    $('liveFlow').textContent = session && session.agitationHeld ? 'HOLD' : (Number.isFinite(flow) ? `${flow.toFixed(1)}g/s` : '—');
    $('liveFlow').style.color = session && session.agitationHeld ? 'var(--agitate)' : flowColor(Math.max(0, finite(flow)));
    $('flowMetricLabel').textContent = session ? `FLOW · PEAK ${session.peakFlow.toFixed(1)}` : 'FLOW';

    const stage = currentStage();
    const idleStage = recipe.stages[0];
    const shownStage = stage || idleStage;
    const meta = TYPE_META[shownStage.type];
    $('brewHero').style.setProperty('--stage-color', meta.color);
    $('stageKicker').textContent = session
      ? (session.status === 'finished' ? 'Complete' : `${meta.label} · ${session.currentIndex + 1} of ${recipe.stages.length}`)
      : 'Ready';
    $('activeStageName').textContent = session && session.status === 'finished' ? 'Brew complete' : shownStage.name;
    $('totalClock').textContent = formatClock(totalElapsedMs() / 1000);
    $('timelineClock').textContent = session ? formatClock(totalElapsedMs() / 1000) : formatClock(totals.seconds);

    const progress = stageProgress(stage);
    $('stageMetricLabel').textContent = progress.metricLabel;
    $('stageMetric').textContent = progress.current;
    $('stageProgress').style.width = `${Math.round(progress.ratio * 100)}%`;
    $('stageProgressText').textContent = session ? progress.detail : 'Ready to begin';
    $('stageTargetText').textContent = session ? progress.target : stageTargetDetail(idleStage);

    const startButton = $('startPauseButton');
    if (!session || session.status === 'finished') startButton.innerHTML = '<i class="fa fa-play mr-2"></i>Start';
    else if (session.status === 'paused') startButton.innerHTML = '<i class="fa fa-play mr-2"></i>Resume';
    else startButton.innerHTML = '<span class="symbol">Ⅱ</span> Pause';
    $('tareButton').disabled = Boolean(session && session.status === 'running');
    $('previousStageButton').disabled = !session || session.currentIndex <= 0 || session.status === 'finished';
    $('nextStageButton').disabled = !session || session.status === 'finished';
    $('finishButton').disabled = !session || session.status === 'finished';
    $('agitationButton').disabled = !session || session.status !== 'running';
    $('agitationButton').classList.toggle('active', Boolean(session && session.agitationHeld));
    $('agitationButton').innerHTML = session && session.agitationHeld
      ? '<span class="symbol">↻</span> Swirling… release to resume'
      : '<span class="symbol">↻</span> Hold while swirling';
    renderTimeline();
    drawBrewChart();
  }

  function ensurePreparedRecipeOnly() {
    if (!preparedRecipe) preparedRecipe = clone(selectedRecipe());
  }

  function maybeAutoAdvance() {
    const stage = currentStage();
    if (!stage || !session || session.status !== 'running' || !stage.autoAdvance) return;
    if (session.firmware) return;
    const elapsed = stageElapsedMs() / 1000;
    if (elapsed < 0.6) return;
    if (stage.type === 'pour') {
      const added = Math.max(0, finite(lastSnapshot.weight) - session.stageBaselineWeight);
      if (added >= finite(stage.targetGrams)) nextStage(true);
      return;
    }
    if (finite(stage.durationSec) > 0 && elapsed >= finite(stage.durationSec)) nextStage(true);
  }

  function recipeFromFirmware(state) {
    const source = state && state.recipe;
    if (!source || !Array.isArray(source.stages) || !source.stages.length) return null;
    return {
      id: preparedRecipe && preparedRecipe.id || selectedRecipeId || makeId('recipe'),
      name: String(source.name || 'Scale recipe'),
      stages: source.stages.map((stage, index) => normalizeStage({
        id: preparedRecipe && preparedRecipe.stages[index] && preparedRecipe.stages[index].id || makeId('stage'),
        type: stage.type,
        name: stage.name,
        targetGrams: finite(stage.target_g),
        durationSec: finite(stage.duration_ms) / 1000,
        flowMin: finite(stage.flow_min_gps),
        flowMax: finite(stage.flow_max_gps),
        autoAdvance: stage.auto_advance !== false
      }))
    };
  }

  function applyFirmwareSession(state) {
    if (!state || state.supported !== true) return;
    firmwareSessionAvailable = true;
    const incomingTransition = finite(state.transition_count, -1);
    if (Date.now() < firmwareCommandPendingUntil && incomingTransition === lastFirmwareTransition) return;
    if (incomingTransition !== lastFirmwareTransition) firmwareCommandPendingUntil = 0;
    lastFirmwareTransition = incomingTransition;

    const firmwareRecipe = recipeFromFirmware(state) || preparedRecipe;
    if (!firmwareRecipe) return;
    preparedRecipe = firmwareRecipe;
    if (state.status === 'idle' || state.status === 'ready') {
      session = null;
      return;
    }

    const now = Date.now();
    const prior = session && session.firmware ? session : null;
    const stageElapsed = Math.max(0, finite(state.stage_elapsed_ms));
    const totalElapsed = Math.max(0, finite(state.total_elapsed_ms));
    const paused = state.status === 'paused';
    const finished = state.status === 'finished';
    const stateStages = state.recipe && Array.isArray(state.recipe.stages) ? state.recipe.stages : [];
    const results = firmwareRecipe.stages.map((stage, index) => {
      const result = stateStages[index] && stateStages[index].result;
      return result ? {
        type: stage.type,
        name: stage.name,
        targetGrams: stage.type === 'pour' ? stage.targetGrams : null,
        actualAddedGrams: stage.type === 'pour' ? finite(result.actual_added_g) : null,
        durationSeconds: finite(result.duration_ms) / 1000,
        averageFlow: finite(result.average_flow_gps),
        completedAtSeconds: finite(result.completed_at_ms) / 1000
      } : (prior && prior.results[index] || null);
    });

    session = {
      firmware: true,
      status: state.status,
      currentIndex: clamp(finite(state.current_stage_index), 0, firmwareRecipe.stages.length - 1),
      startedAt: now - totalElapsed,
      stageStartedAt: now - stageElapsed,
      stagePausedMs: 0,
      totalPausedMs: 0,
      pauseStartedAt: paused ? now : 0,
      stageBaselineWeight: finite(state.stage_baseline_weight_g),
      flowSum: 0,
      flowSamples: 0,
      peakFlow: finite(state.peak_flow_gps),
      samples: prior && prior.samples || [],
      lastRecordedAt: prior && prior.lastRecordedAt || 0,
      agitationHeld: prior && prior.agitationHeld || false,
      frozenDisplay: prior && prior.frozenDisplay || null,
      referenceSamples: prior && prior.referenceSamples || loadReferenceSamples(firmwareRecipe.id),
      results,
      finishedAt: finished ? now : 0
    };

    if (finished && recordedFirmwareFinish !== incomingTransition) {
      recordedFirmwareFinish = incomingTransition;
      const record = {
        recipe: preparedRecipe,
        results: session.results,
        samples: session.samples,
        durationSeconds: totalElapsed / 1000,
        finishedAt: new Date().toISOString()
      };
      try { localStorage.setItem(LAST_BREW_KEY, JSON.stringify(record)); } catch (_) { /* best effort */ }
      $('brewMessage').textContent = 'Brew complete.';
      vibrate([40, 50, 40]);
    }
  }

  function acceptSnapshot(snapshot) {
    if (!snapshot || typeof snapshot !== 'object') return;
    lastSnapshot = snapshot;
    lastSnapshotAt = Date.now();
    $('connectionPill').textContent = 'Online';
    $('connectionPill').className = 'status-pill ok';
    $('headerSubtitle').textContent = `${snapshot.device_board || 'WMB+'} · ${snapshot.device_version || 'connected'}`;
    applyFirmwareSession(snapshot.pour_over);
    if (session && session.status === 'running') {
      const flow = finite(snapshot.flowrate, NaN);
      if (!session.firmware && Number.isFinite(flow) && !session.agitationHeld) {
        session.flowSum += Math.max(0, flow);
        session.flowSamples += 1;
        session.peakFlow = Math.max(session.peakFlow, Math.max(0, flow));
      }
      recordSample(snapshot);
      if (!session.firmware) maybeAutoAdvance();
    }
    if (!$('brewView').hidden) renderLive();
  }

  function setAgitationHeld(held) {
    if (!session || session.status !== 'running' || session.agitationHeld === held) return;
    session.agitationHeld = held;
    session.frozenDisplay = held ? clone(lastSnapshot) : null;
    $('brewMessage').textContent = held ? 'Agitation marked; display held while samples continue.' : 'Live display resumed.';
    vibrate(held ? 20 : 10);
    renderLive();
  }

  async function tareScale() {
    const button = $('tareButton');
    button.disabled = true;
    $('brewMessage').textContent = 'Taring…';
    try {
      const response = await fetch('/api/tare', { method: 'POST' });
      if (!response.ok) throw new Error(`tare ${response.status}`);
      $('brewMessage').textContent = 'Tare scheduled. Keep the brewer still.';
      vibrate(25);
    } catch (_) {
      $('brewMessage').textContent = 'Tare failed: scale unavailable.';
      vibrate([50, 40, 50]);
    } finally {
      window.setTimeout(() => { button.disabled = false; }, 1200);
    }
  }

  async function toggleWakeLock() {
    const button = $('wakeButton');
    try {
      if (wakeLock) {
        await wakeLock.release();
        wakeLock = null;
        button.innerHTML = '<span class="symbol">◉</span> Keep Awake';
        return;
      }
      if (!('wakeLock' in navigator)) throw new Error('unsupported');
      wakeLock = await navigator.wakeLock.request('screen');
      wakeLock.addEventListener('release', () => {
        wakeLock = null;
        button.innerHTML = '<span class="symbol">◉</span> Keep Awake';
      });
      button.innerHTML = '<span class="symbol">○</span> Release Wake';
    } catch (_) {
      $('brewMessage').textContent = 'Screen wake lock unavailable.';
    }
  }

  function vibrate(pattern) {
    try { navigator.vibrate?.(pattern); } catch (_) { /* optional */ }
  }

  function bindEvents() {
    $('builderTab').addEventListener('click', () => setView('builder'));
    $('brewTab').addEventListener('click', () => setView('brew'));
    $('recipeSelect').addEventListener('change', event => {
      selectedRecipeId = event.target.value;
      persistRecipes(false);
      renderBuilder();
    });
    $('recipeName').addEventListener('input', event => updateRecipeName(event.target.value));
    $('newRecipeButton').addEventListener('click', newRecipe);
    $('duplicateRecipeButton').addEventListener('click', duplicateRecipe);
    $('deleteRecipeButton').addEventListener('click', deleteRecipe);
    $('prepareButton').addEventListener('click', prepareSelectedRecipe);
    document.querySelectorAll('[data-add-stage]').forEach(button => button.addEventListener('click', () => addStage(button.dataset.addStage)));
    $('stageList').addEventListener('input', event => {
      const card = event.target.closest('[data-stage-id]');
      if (card && event.target.dataset.field) updateStage(card.dataset.stageId, event.target.dataset.field, event.target);
    });
    $('stageList').addEventListener('change', event => {
      const card = event.target.closest('[data-stage-id]');
      if (card && event.target.dataset.field) updateStage(card.dataset.stageId, event.target.dataset.field, event.target);
    });
    $('stageList').addEventListener('click', event => {
      const button = event.target.closest('[data-stage-action]');
      const card = event.target.closest('[data-stage-id]');
      if (button && card) stageAction(card.dataset.stageId, button.dataset.stageAction);
    });
    $('tareButton').addEventListener('click', tareScale);
    $('startPauseButton').addEventListener('click', toggleStartPause);
    $('previousStageButton').addEventListener('click', previousStage);
    $('nextStageButton').addEventListener('click', () => nextStage(false));
    $('finishButton').addEventListener('click', finishSession);
    $('wakeButton').addEventListener('click', toggleWakeLock);
    const agitationButton = $('agitationButton');
    agitationButton.addEventListener('pointerdown', event => { event.preventDefault(); setAgitationHeld(true); });
    ['pointerup', 'pointercancel', 'pointerleave'].forEach(type => agitationButton.addEventListener(type, () => setAgitationHeld(false)));
  }

  function startLiveData() {
    if (window.WMBPLive) {
      window.WMBPLive.subscribe(acceptSnapshot);
      window.WMBPLive.fetchJson('/api/dashboard').then(acceptSnapshot).catch(() => {
        $('connectionPill').textContent = 'Offline';
        $('connectionPill').className = 'status-pill bad';
      });
      return;
    }
    const poll = () => fetch('/api/dashboard', { cache: 'no-store' })
      .then(response => response.ok ? response.json() : Promise.reject())
      .then(acceptSnapshot)
      .catch(() => {
        $('connectionPill').textContent = 'Offline';
        $('connectionPill').className = 'status-pill bad';
      });
    poll();
    window.setInterval(poll, 1000);
  }

  function tick() {
    if (lastSnapshotAt && Date.now() - lastSnapshotAt > 3500) {
      $('connectionPill').textContent = 'Offline';
      $('connectionPill').className = 'status-pill bad';
    }
    if (!$('brewView').hidden) {
      if (session && session.status === 'running') maybeAutoAdvance();
      renderLive();
    }
  }

  function init() {
    loadRecipes();
    bindEvents();
    renderBuilder();
    preparedRecipe = clone(selectedRecipe());
    renderLive();
    startLiveData();
    renderTimer = window.setInterval(tick, 250);
    if ('serviceWorker' in navigator && window.isSecureContext) {
      navigator.serviceWorker.register('/sw.js').catch(() => undefined);
    }
  }

  window.addEventListener('pagehide', () => {
    if (renderTimer) window.clearInterval(renderTimer);
  });
  document.addEventListener('DOMContentLoaded', init);
})();
