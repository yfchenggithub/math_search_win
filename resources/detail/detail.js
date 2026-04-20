(function () {
  "use strict";

  const KATEX_DELIMITERS = [
    { left: "$$", right: "$$", display: true },
    { left: "\\[", right: "\\]", display: true },
    { left: "\\(", right: "\\)", display: false },
    { left: "$", right: "$", display: false }
  ];

  const HEAVY_DOM_BATCH_BUDGET_MS = 5.0;
  const HEAVY_DOM_MAX_SECTIONS_PER_BATCH = 1;
  const DEFERRED_KATEX_BATCH_BUDGET_MS = 6.0;
  const DEFERRED_KATEX_MAX_UNITS_PER_BATCH = 3;
  const MATH_HINT_PATTERN = /(\$\$|\\\[|\\\(|\$)/;
  const MATH_HINT_GLOBAL_PATTERN = /(\$\$|\\\[|\\\(|\$)/g;

  const runtime = {
    shellInitialized: false,
    rootClickBound: false,
    currentRequestId: 0,
    currentDetailId: "",
    currentSessionId: 0,
    requestStartedAtMs: 0,
    previousPhaseAtMs: 0,
    sectionNodes: new Map(),
    activeSectionIds: [],
    abortedRequests: new Set(),
    requestDetailMap: new Map(),
    schedulerState: {
      lightPaint: null,
      heavyDom: null,
      deferredKatex: null
    },
    dom: null
  };

  function nowMs() {
    if (typeof performance !== "undefined" && performance && typeof performance.now === "function") {
      return performance.now();
    }
    return Date.now();
  }

  function byId(id) {
    return document.getElementById(id);
  }

  function toNumber(value, fallback) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
  }

  function escapeHtml(value) {
    const text = value == null ? "" : String(value);
    return text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function htmlFromText(text) {
    const raw = text == null ? "" : String(text).trim();
    if (!raw) {
      return "";
    }
    const paragraphs = raw
      .split(/\n{2,}/)
      .map((item) => item.trim())
      .filter(Boolean);
    if (!paragraphs.length) {
      return "";
    }
    return paragraphs
      .map((paragraph) => "<p>" + escapeHtml(paragraph).replace(/\n/g, "<br/>") + "</p>")
      .join("");
  }

  function cacheDomRefs() {
    if (runtime.dom) {
      return runtime.dom;
    }
    runtime.dom = {
      root: byId("detail-root"),
      emptyState: byId("detail-empty-state"),
      emptyText: byId("detail-empty-text"),
      errorState: byId("detail-error-state"),
      errorText: byId("detail-error-text"),
      content: byId("detail-content"),
      header: byId("detail-header"),
      title: byId("detail-title"),
      meta: byId("detail-meta"),
      tags: byId("detail-tags"),
      core: byId("detail-core"),
      condition: byId("detail-condition"),
      remarks: byId("detail-remarks"),
      vars: byId("detail-vars"),
      sections: byId("detail-sections")
    };
    return runtime.dom;
  }

  function reportPerf(phase, extra) {
    const payload = extra && typeof extra === "object" ? extra : {};
    const now = nowMs();
    const requestId = payload.requestId != null ? toNumber(payload.requestId, runtime.currentRequestId) : runtime.currentRequestId;
    const detailId = payload.detailId != null ? String(payload.detailId || "").trim() : runtime.currentDetailId;

    let elapsedFromRequest = 0;
    if (runtime.requestStartedAtMs > 0 && requestId === runtime.currentRequestId) {
      elapsedFromRequest = Math.max(0, now - runtime.requestStartedAtMs);
    }
    const deltaFromPrev = runtime.previousPhaseAtMs > 0 ? Math.max(0, now - runtime.previousPhaseAtMs) : 0;
    runtime.previousPhaseAtMs = now;

    const extras = [];
    Object.keys(payload).forEach((key) => {
      if (key === "requestId" || key === "detailId") {
        return;
      }
      extras.push(key + "=" + String(payload[key]));
    });

    let line =
      "[perf][detail] id=" +
      (detailId || "-") +
      " req=" +
      String(requestId || 0) +
      " phase=" +
      String(phase || "unknown") +
      " t=" +
      elapsedFromRequest.toFixed(1) +
      "ms dt=" +
      deltaFromPrev.toFixed(1) +
      "ms";
    if (extras.length) {
      line += " " + extras.join(" ");
    }

    if (typeof console !== "undefined" && typeof console.debug === "function") {
      console.debug(line);
    }
    return line;
  }

  function isRequestStale(requestId) {
    const normalized = toNumber(requestId, 0);
    if (!normalized) {
      return false;
    }
    return normalized !== runtime.currentRequestId;
  }

  function abortIfStale(requestId) {
    if (!isRequestStale(requestId)) {
      return false;
    }
    const staleDetailId = runtime.requestDetailMap.has(requestId)
      ? runtime.requestDetailMap.get(requestId)
      : runtime.currentDetailId;
    if (!runtime.abortedRequests.has(requestId)) {
      runtime.abortedRequests.add(requestId);
      reportPerf("render_aborted_due_to_newer_request", { requestId: requestId, detailId: staleDetailId });
    }
    const dom = cacheDomRefs();
    if (dom.root) {
      dom.root.classList.add("is-stale-request");
      dom.root.classList.remove("is-rendering-light", "is-rendering-heavy");
    }
    return true;
  }

  function isSessionStale(sessionId) {
    const normalized = toNumber(sessionId, 0);
    if (!normalized) {
      return false;
    }
    return normalized !== runtime.currentSessionId;
  }

  function isRenderContextStale(requestId, sessionId) {
    return isRequestStale(requestId) || isSessionStale(sessionId);
  }

  function markDeferredSupersededOnce(state) {
    if (!state || state.kind !== "deferredKatex" || state.supersededLogged) {
      return;
    }
    state.supersededLogged = true;
    reportPerf("deferred_skipped_due_to_superseded", {
      requestId: state.requestId,
      detailId: state.detailId,
      reason: "request_or_session_stale",
      remainingUnits: Math.max(0, state.queueLength - state.cursor),
      remainingTargets: Math.max(0, state.pendingTargets || 0)
    });
  }

  function stopScheduler(kind, reason) {
    if (!kind || !runtime.schedulerState || !runtime.schedulerState[kind]) {
      return;
    }
    const state = runtime.schedulerState[kind];
    state.cancelled = true;
    if (state.rafId) {
      cancelAnimationFrame(state.rafId);
      state.rafId = 0;
    }
    if (kind === "deferredKatex" && !state.completed) {
      reportPerf("deferred_cancelled", {
        requestId: state.requestId,
        detailId: state.detailId,
        reason: String(reason || "cancelled"),
        remainingUnits: Math.max(0, state.queueLength - state.cursor),
        remainingTargets: Math.max(0, state.pendingTargets || 0)
      });
    }
    runtime.schedulerState[kind] = null;
  }

  function stopAllSchedulers(reason) {
    stopScheduler("lightPaint", reason);
    stopScheduler("heavyDom", reason);
    stopScheduler("deferredKatex", reason);
  }

  function scheduleBatchedQueue(options) {
    const config = options && typeof options === "object" ? options : {};
    const kind = String(config.kind || "heavyDom");
    const requestId = toNumber(config.requestId, 0);
    const detailId = String(config.detailId || runtime.currentDetailId || "").trim();
    const sessionId = toNumber(config.sessionId, runtime.currentSessionId);
    const queue = Array.isArray(config.queue) ? config.queue : [];
    const batchBudgetMs = Math.max(1, toNumber(config.batchBudgetMs, 8));
    const maxItemsPerBatch = Math.max(1, toNumber(config.maxItemsPerBatch, Number.MAX_SAFE_INTEGER));
    const onTask = typeof config.onTask === "function" ? config.onTask : null;
    const onDone = typeof config.onDone === "function" ? config.onDone : null;
    const onBatchStart = typeof config.onBatchStart === "function" ? config.onBatchStart : null;
    const onBatchDone = typeof config.onBatchDone === "function" ? config.onBatchDone : null;
    const onStale = typeof config.onStale === "function" ? config.onStale : null;
    const initialPendingTargets = Math.max(0, toNumber(config.initialPendingTargets, 0));

    stopScheduler(kind, "replace_queue");

    const state = {
      kind: kind,
      requestId: requestId,
      detailId: detailId,
      sessionId: sessionId,
      queueLength: queue.length,
      cursor: 0,
      batchIndex: 0,
      rafId: 0,
      cancelled: false,
      completed: false,
      supersededLogged: false,
      pendingTargets: initialPendingTargets
    };
    runtime.schedulerState[kind] = state;

    function finalize(completed) {
      if (state.cancelled) {
        runtime.schedulerState[kind] = null;
        return;
      }
      state.completed = !!completed;
      runtime.schedulerState[kind] = null;
      if (state.completed && onDone) {
        onDone(state);
      }
    }

    function runBatch() {
      if (state.cancelled) {
        runtime.schedulerState[kind] = null;
        return;
      }
      if (isRenderContextStale(state.requestId, state.sessionId)) {
        if (kind === "deferredKatex") {
          markDeferredSupersededOnce(state);
        }
        if (onStale) {
          onStale(state);
        }
        runtime.schedulerState[kind] = null;
        return;
      }

      const batchNumber = state.batchIndex + 1;
      const batchStart = nowMs();
      if (onBatchStart) {
        onBatchStart(state, batchNumber);
      }

      let processed = 0;
      while (state.cursor < queue.length) {
        if (isRenderContextStale(state.requestId, state.sessionId)) {
          if (kind === "deferredKatex") {
            markDeferredSupersededOnce(state);
          }
          if (onStale) {
            onStale(state);
          }
          runtime.schedulerState[kind] = null;
          return;
        }

        const item = queue[state.cursor];
        state.cursor += 1;
        processed += 1;
        if (onTask) {
          onTask(item, state);
        }

        if (processed >= maxItemsPerBatch) {
          break;
        }
        if (nowMs() - batchStart >= batchBudgetMs) {
          break;
        }
      }

      const batchCostMs = Math.max(0, nowMs() - batchStart);
      if (onBatchDone) {
        onBatchDone(state, {
          batch: batchNumber,
          processed: processed,
          batchMs: batchCostMs,
          remainingItems: Math.max(0, queue.length - state.cursor)
        });
      }

      state.batchIndex += 1;
      if (state.cursor >= queue.length) {
        finalize(true);
        return;
      }

      state.rafId = requestAnimationFrame(runBatch);
    }

    if (!queue.length) {
      finalize(true);
      return state;
    }

    state.rafId = requestAnimationFrame(runBatch);
    return state;
  }

  function setState(state, message) {
    const dom = cacheDomRefs();
    if (!dom.root) {
      return;
    }

    if (dom.emptyState) {
      dom.emptyState.hidden = state !== "empty";
    }
    if (dom.errorState) {
      dom.errorState.hidden = state !== "error";
    }
    if (dom.content) {
      dom.content.hidden = state !== "content";
    }
    if (state === "empty" && dom.emptyText) {
      dom.emptyText.textContent = message || "请从左侧结果中选择一条结论查看详情。";
    }
    if (state === "error" && dom.errorText) {
      dom.errorText.textContent = message || "详情暂时不可用。";
    }
  }

  function renderMeta(payload) {
    const dom = cacheDomRefs();
    if (!dom.meta) {
      return;
    }
    const module = String(payload.module || "").trim();
    const category = String(payload.category || "").trim();
    const detailId = String(payload.detailId || "").trim();
    const items = [];
    if (detailId) {
      items.push("ID " + detailId);
    }
    if (module) {
      items.push("模块 " + module);
    }
    if (category) {
      items.push("分类 " + category);
    }
    dom.meta.innerHTML = items
      .map(function (item) {
        return "<span class=\"detail-meta-item\">" + escapeHtml(item) + "</span>";
      })
      .join("");
  }

  function renderTags(tags) {
    const dom = cacheDomRefs();
    if (!dom.tags) {
      return;
    }
    const rows = Array.isArray(tags) ? tags : [];
    dom.tags.innerHTML = rows
      .map(function (rawTag) {
        const tag = String(rawTag || "").trim();
        if (!tag) {
          return "";
        }
        return "<span class=\"detail-tag\">" + escapeHtml(tag) + "</span>";
      })
      .join("");
  }

  function renderFixedSection(node, title, html) {
    if (!node) {
      return;
    }
    const normalizedHtml = String(html || "").trim();
    if (!normalizedHtml) {
      node.hidden = true;
      node.innerHTML = "";
      return;
    }
    node.hidden = false;
    node.innerHTML =
      "<h3 class=\"detail-fixed-section-title\">" +
      escapeHtml(title) +
      "</h3><div class=\"detail-rich-text\">" +
      normalizedHtml +
      "</div>";
    node.dataset.katexRequestId = "";
  }

  function renderVars(vars) {
    const dom = cacheDomRefs();
    if (!dom.vars) {
      return;
    }
    const rows = Array.isArray(vars) ? vars : [];
    if (!rows.length) {
      dom.vars.hidden = true;
      dom.vars.innerHTML = "";
      return;
    }
    const html = rows
      .map(function (row) {
        const name = String((row && row.name) || "").trim();
        const latex = String((row && row.latex) || "").trim();
        const description = String((row && row.description) || "").trim();
        const required = !!(row && row.required);
        if (!name && !latex && !description) {
          return "";
        }
        return (
          "<li class=\"detail-var-item\">" +
          "<div class=\"detail-var-head\">" +
          (name ? "<span class=\"detail-var-name\">" + escapeHtml(name) + "</span>" : "") +
          (latex ? "<span class=\"detail-var-latex\">\\(" + escapeHtml(latex) + "\\)</span>" : "") +
          (required ? "<span class=\"detail-var-required\">必需</span>" : "") +
          "</div>" +
          (description ? "<div class=\"detail-var-desc\">" + escapeHtml(description) + "</div>" : "") +
          "</li>"
        );
      })
      .join("");
    if (!html.trim()) {
      dom.vars.hidden = true;
      dom.vars.innerHTML = "";
      return;
    }
    dom.vars.hidden = false;
    dom.vars.innerHTML = "<h3 class=\"detail-fixed-section-title\">变量</h3><ul class=\"detail-vars-list\">" + html + "</ul>";
    dom.vars.dataset.katexRequestId = "";
  }

  function ensureSectionNode(sectionId) {
    const dom = cacheDomRefs();
    if (!dom.sections) {
      return null;
    }
    const normalizedId = String(sectionId || "").trim() || "section";
    if (runtime.sectionNodes.has(normalizedId)) {
      return runtime.sectionNodes.get(normalizedId);
    }
    const sectionNode = document.createElement("section");
    sectionNode.className = "detail-section";
    sectionNode.dataset.sectionId = normalizedId;
    const headerNode = document.createElement("h3");
    headerNode.className = "detail-section-header";
    const bodyNode = document.createElement("div");
    bodyNode.className = "detail-section-body";
    sectionNode.appendChild(headerNode);
    sectionNode.appendChild(bodyNode);
    dom.sections.appendChild(sectionNode);
    const entry = {
      node: sectionNode,
      header: headerNode,
      body: bodyNode
    };
    runtime.sectionNodes.set(normalizedId, entry);
    return entry;
  }

  function patchSection(sectionId, sectionData) {
    const entry = ensureSectionNode(sectionId);
    if (!entry || !entry.node) {
      return null;
    }
    const node = entry.node;
    const header = entry.header;
    const body = entry.body;
    const label = String((sectionData && sectionData.label) || "").trim() || "内容";
    const html = String((sectionData && sectionData.html) || "").trim();
    const isPlaceholder = !!(sectionData && sectionData.isPlaceholder);
    const order = toNumber(sectionData && sectionData.order, 0);

    if (header) {
      header.textContent = label;
    }
    if (body) {
      if (isPlaceholder) {
        body.innerHTML = "<p class=\"detail-section-placeholder\">正在补全内容...</p>";
      } else {
        body.innerHTML = "<div class=\"detail-rich-text\">" + html + "</div>";
      }
      body.dataset.katexRequestId = "";
      body.dataset.sectionId = String(sectionId || "").trim();
      body.dataset.sectionState = isPlaceholder ? "placeholder" : "ready";
    }
    node.hidden = false;
    node.style.order = String(order);
    return entry;
  }

  function beginRenderSession(payload) {
    const requestId = toNumber(payload.requestId, 0);
    const detailId = String(payload.detailId || "").trim();
    if (!requestId || !detailId) {
      return { ok: false, error: "invalid_request_payload", requestId: requestId, detailId: detailId };
    }

    stopAllSchedulers("new_render_session");
    runtime.currentSessionId += 1;
    runtime.currentRequestId = requestId;
    runtime.currentDetailId = detailId;
    runtime.requestDetailMap.set(requestId, detailId);
    runtime.requestStartedAtMs = nowMs();
    runtime.previousPhaseAtMs = runtime.requestStartedAtMs;

    const dom = cacheDomRefs();
    if (dom.root) {
      dom.root.dataset.requestId = String(requestId);
      dom.root.dataset.sessionId = String(runtime.currentSessionId);
      dom.root.classList.remove("is-stale-request");
      dom.root.classList.add("is-shell-ready", "is-rendering-light");
      dom.root.classList.remove("is-rendering-heavy");
    }
    reportPerf("render_request_received", { requestId: requestId, detailId: detailId });
    return { ok: true, requestId: requestId, detailId: detailId, sessionId: runtime.currentSessionId };
  }

  function renderLightSections(payload) {
    const dom = cacheDomRefs();
    // Phase 1: render only the minimum above-the-fold content so users see immediate feedback.
    reportPerf("render_light_start", { requestId: payload.requestId, detailId: payload.detailId });

    setState("content");
    if (dom.title) {
      dom.title.textContent = String(payload.title || "").trim() || "未命名结论";
    }
    renderMeta(payload);
    renderTags(payload.tags);

    const statementHtml = String(payload.statementHtml || payload.coreHtml || "").trim();
    const conditionHtml = String(payload.conditionHtml || "").trim();
    const remarkHtml = String(payload.remarkHtml || "").trim();

    renderFixedSection(dom.core, "核心结论", statementHtml || htmlFromText(""));
    renderFixedSection(dom.condition, "条件", conditionHtml);
    renderFixedSection(dom.remarks, "备注", remarkHtml);
    renderVars(payload.vars);

    const sections = Array.isArray(payload.sections) ? payload.sections : [];
    const activeIds = [];
    sections.forEach(function (section, index) {
      const id = String((section && section.id) || "").trim();
      if (!id) {
        return;
      }
      activeIds.push(id);
      patchSection(id, {
        label: section.label,
        isPlaceholder: true,
        order: index + 1
      });
    });

    runtime.activeSectionIds = activeIds;
    runtime.sectionNodes.forEach(function (sectionEntry, id) {
      if (!sectionEntry || !sectionEntry.node) {
        return;
      }
      sectionEntry.node.hidden = activeIds.indexOf(id) === -1;
    });

    reportPerf("render_light_done", { requestId: payload.requestId, detailId: payload.detailId, sections: activeIds.length });
  }

  function flushLightPaint(requestId, sessionId, callback) {
    stopScheduler("lightPaint", "replace_light_paint");
    const state = {
      kind: "lightPaint",
      requestId: requestId,
      detailId: runtime.currentDetailId,
      sessionId: sessionId,
      rafId: 0,
      cancelled: false,
      completed: false
    };
    runtime.schedulerState.lightPaint = state;

    state.rafId = requestAnimationFrame(function () {
      runtime.schedulerState.lightPaint = null;
      if (state.cancelled) {
        return;
      }
      if (isRenderContextStale(requestId, sessionId)) {
        abortIfStale(requestId);
        return;
      }
      reportPerf("first_meaningful_paint_dispatch", { requestId: requestId, detailId: runtime.currentDetailId });
      if (typeof callback === "function") {
        callback();
      }
    });
  }

  function renderKatexInElement(element, requestId, markRequestId) {
    if (!element || typeof window.renderMathInElement !== "function") {
      return 0;
    }
    if (String(element.dataset.katexRequestId || "") === String(requestId)) {
      return 0;
    }

    const startedAt = nowMs();
    try {
      window.renderMathInElement(element, {
        delimiters: KATEX_DELIMITERS,
        throwOnError: false
      });
    } catch (error) {
      if (typeof console !== "undefined" && typeof console.warn === "function") {
        console.warn("KaTeX render failed", error);
      }
    }
    if (markRequestId !== false) {
      element.dataset.katexRequestId = String(requestId);
    }
    return nowMs() - startedAt;
  }

  function hasMathHint(text) {
    return MATH_HINT_PATTERN.test(String(text || ""));
  }

  function countMathHints(text) {
    const raw = String(text || "");
    if (!raw) {
      return 0;
    }
    const matches = raw.match(MATH_HINT_GLOBAL_PATTERN);
    return Array.isArray(matches) ? matches.length : 0;
  }

  function isElementVisibleInViewport(element) {
    if (!element || typeof element.getBoundingClientRect !== "function") {
      return false;
    }
    const viewportHeight = window.innerHeight || 0;
    const rect = element.getBoundingClientRect();
    return rect.top <= viewportHeight && rect.bottom >= 0;
  }

  function collectKatexUnits(target) {
    if (!target) {
      return [];
    }
    const richRoot = target.querySelector(".detail-rich-text") || target;
    const units = [];
    const candidates = richRoot.querySelectorAll("p, li, blockquote, pre, code, td, th, .detail-math-block");
    if (candidates && candidates.length) {
      candidates.forEach(function (candidate) {
        if (!candidate || !hasMathHint(candidate.textContent || "")) {
          return;
        }
        units.push(candidate);
      });
    }
    if (!units.length && hasMathHint(richRoot.textContent || "")) {
      units.push(richRoot);
    }
    return units;
  }

  function collectVisibleTargets() {
    const dom = cacheDomRefs();
    const targets = [];
    [dom.core, dom.condition, dom.remarks, dom.vars].forEach(function (node) {
      if (!node || node.hidden) {
        return;
      }
      targets.push(node);
    });

    runtime.activeSectionIds.forEach(function (sectionId) {
      const sectionEntry = runtime.sectionNodes.get(sectionId);
      if (!sectionEntry || !sectionEntry.node || sectionEntry.node.hidden) {
        return;
      }
      const body = sectionEntry.body;
      if (!body || String(body.dataset.sectionState || "") !== "ready") {
        return;
      }
      if (isElementVisibleInViewport(sectionEntry.node)) {
        targets.push(body);
      }
    });
    return targets;
  }

  function renderKatexVisibleFirst(requestId, sessionId) {
    if (abortIfStale(requestId) || isSessionStale(sessionId)) {
      return;
    }
    reportPerf("katex_visible_start", { requestId: requestId, detailId: runtime.currentDetailId });
    const targets = collectVisibleTargets();
    let totalMs = 0;
    targets.forEach(function (target) {
      totalMs += renderKatexInElement(target, requestId, true);
    });
    reportPerf("katex_visible_done", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      targets: targets.length,
      katexMs: totalMs.toFixed(1)
    });
  }

  function collectDeferredTargetEntries(requestId) {
    const deferredTargets = [];
    runtime.activeSectionIds.forEach(function (sectionId) {
      const sectionEntry = runtime.sectionNodes.get(sectionId);
      if (!sectionEntry || !sectionEntry.node || sectionEntry.node.hidden || !sectionEntry.body) {
        return;
      }
      const body = sectionEntry.body;
      if (String(body.dataset.sectionState || "") !== "ready") {
        return;
      }
      if (String(body.dataset.katexRequestId || "") === String(requestId)) {
        return;
      }
      const htmlSnapshot = String(body.innerHTML || "");
      const textSnapshot = String(body.textContent || "");
      deferredTargets.push({
        sectionId: sectionId,
        body: body,
        htmlLen: htmlSnapshot.length,
        textChars: textSnapshot.length,
        mathHintCount: countMathHints(textSnapshot),
        inViewport: isElementVisibleInViewport(sectionEntry.node),
        units: [],
        formulaNodeCount: 0,
        processedUnits: 0,
        katexMs: 0,
        started: false,
        done: false
      });
    });

    reportPerf("deferred_collect_targets", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      targets: deferredTargets.length
    });

    deferredTargets.sort(function (lhs, rhs) {
      if (lhs.inViewport === rhs.inViewport) {
        return 0;
      }
      return lhs.inViewport ? -1 : 1;
    });

    const queue = [];
    let skippedTargets = 0;
    deferredTargets.forEach(function (entry) {
      entry.units = collectKatexUnits(entry.body);
      entry.formulaNodeCount = entry.units.length;
      if (!entry.formulaNodeCount) {
        skippedTargets += 1;
        return;
      }
      entry.units.forEach(function (unit) {
        queue.push({
          target: entry,
          unit: unit
        });
      });
    });

    reportPerf("deferred_queue_built", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      targets: deferredTargets.length,
      queueUnits: queue.length,
      visibleTargets: deferredTargets.filter(function (entry) {
        return entry.inViewport;
      }).length,
      noMathTargets: skippedTargets
    });

    return {
      targets: deferredTargets,
      queue: queue
    };
  }

  function markTargetKatexStarted(targetEntry, requestId) {
    if (!targetEntry || targetEntry.started) {
      return;
    }
    targetEntry.started = true;
    reportPerf("per_target_katex_start", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      target: targetEntry.sectionId,
      formulaNodes: targetEntry.formulaNodeCount,
      htmlLen: targetEntry.htmlLen,
      textChars: targetEntry.textChars,
      mathHints: targetEntry.mathHintCount,
      visible: targetEntry.inViewport ? 1 : 0
    });
    reportPerf("heavy_section_katex_render", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      sectionId: targetEntry.sectionId,
      stage: "start",
      formulaNodes: targetEntry.formulaNodeCount
    });
  }

  function markTargetKatexDone(targetEntry, requestId) {
    if (!targetEntry || targetEntry.done) {
      return;
    }
    targetEntry.done = true;
    if (targetEntry.body) {
      targetEntry.body.dataset.katexRequestId = String(requestId);
    }
    reportPerf("per_target_katex_done", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      target: targetEntry.sectionId,
      formulaNodes: targetEntry.formulaNodeCount,
      htmlLen: targetEntry.htmlLen,
      textChars: targetEntry.textChars,
      mathHints: targetEntry.mathHintCount,
      renderedUnits: targetEntry.processedUnits,
      katexMs: targetEntry.katexMs.toFixed(1)
    });
    reportPerf("heavy_section_katex_render", {
      requestId: requestId,
      detailId: runtime.currentDetailId,
      sectionId: targetEntry.sectionId,
      stage: "done",
      renderedUnits: targetEntry.processedUnits,
      katexMs: targetEntry.katexMs.toFixed(1)
    });
  }

  function renderKatexDeferred(requestId, sessionId, onDone) {
    if (abortIfStale(requestId) || isSessionStale(sessionId)) {
      return;
    }
    reportPerf("katex_deferred_start", { requestId: requestId, detailId: runtime.currentDetailId });

    const queueBuildResult = collectDeferredTargetEntries(requestId);
    const targets = queueBuildResult.targets;
    const queue = queueBuildResult.queue;
    let pendingTargets = 0;
    targets.forEach(function (targetEntry) {
      if (!targetEntry.formulaNodeCount) {
        markTargetKatexStarted(targetEntry, requestId);
        markTargetKatexDone(targetEntry, requestId);
        return;
      }
      pendingTargets += 1;
    });

    if (!pendingTargets || !queue.length) {
      const totalKatexMs = targets.reduce(function (acc, entry) {
        return acc + entry.katexMs;
      }, 0);
      reportPerf("katex_deferred_done", {
        requestId: requestId,
        detailId: runtime.currentDetailId,
        targets: targets.length,
        queueUnits: queue.length,
        katexMs: totalKatexMs.toFixed(1)
      });
      if (typeof onDone === "function") {
        onDone();
      }
      return;
    }

    scheduleBatchedQueue({
      kind: "deferredKatex",
      requestId: requestId,
      detailId: runtime.currentDetailId,
      sessionId: sessionId,
      queue: queue,
      batchBudgetMs: DEFERRED_KATEX_BATCH_BUDGET_MS,
      maxItemsPerBatch: DEFERRED_KATEX_MAX_UNITS_PER_BATCH,
      initialPendingTargets: pendingTargets,
      onBatchStart: function (state, batchNumber) {
        reportPerf("deferred_batch_start", {
          requestId: requestId,
          detailId: runtime.currentDetailId,
          batch: batchNumber,
          remainingUnits: Math.max(0, state.queueLength - state.cursor),
          remainingTargets: Math.max(0, state.pendingTargets || 0)
        });
      },
      onTask: function (task, state) {
        if (!task || !task.target || !task.unit) {
          return;
        }
        const targetEntry = task.target;
        markTargetKatexStarted(targetEntry, requestId);
        targetEntry.katexMs += renderKatexInElement(task.unit, requestId, false);
        targetEntry.processedUnits += 1;
        if (targetEntry.processedUnits >= targetEntry.formulaNodeCount) {
          markTargetKatexDone(targetEntry, requestId);
          state.pendingTargets = Math.max(0, (state.pendingTargets || 0) - 1);
        }
      },
      onBatchDone: function (state, stats) {
        reportPerf("deferred_batch_done", {
          requestId: requestId,
          detailId: runtime.currentDetailId,
          batch: stats.batch,
          processedUnits: stats.processed,
          batchMs: stats.batchMs.toFixed(1),
          remainingUnits: stats.remainingItems,
          remainingTargets: Math.max(0, state.pendingTargets || 0)
        });
      },
      onStale: function () {
        abortIfStale(requestId);
      },
      onDone: function (state) {
        const totalKatexMs = targets.reduce(function (acc, entry) {
          return acc + entry.katexMs;
        }, 0);
        reportPerf("katex_deferred_done", {
          requestId: requestId,
          detailId: runtime.currentDetailId,
          targets: targets.length,
          queueUnits: queue.length,
          batches: state.batchIndex,
          katexMs: totalKatexMs.toFixed(1)
        });
        if (typeof onDone === "function") {
          onDone();
        }
      }
    });
  }

  function renderHeavySections(payload, sessionId) {
    const requestId = toNumber(payload.requestId, 0);
    if (abortIfStale(requestId) || isSessionStale(sessionId)) {
      return;
    }

    reportPerf("render_heavy_sections_start", { requestId: requestId, detailId: runtime.currentDetailId });
    const sourceSections = Array.isArray(payload.sections) ? payload.sections : [];
    const sections = [];
    sourceSections.forEach(function (section, index) {
      const sectionId = String((section && section.id) || "").trim();
      if (!sectionId) {
        return;
      }
      const html = String((section && section.html) || "").trim();
      sections.push({
        sectionId: sectionId,
        label: section && section.label,
        html: html,
        htmlLen: html.length,
        textChars: html.replace(/<[^>]*>/g, "").length,
        order: index + 1
      });
    });

    scheduleBatchedQueue({
      kind: "heavyDom",
      requestId: requestId,
      detailId: runtime.currentDetailId,
      sessionId: sessionId,
      queue: sections,
      batchBudgetMs: HEAVY_DOM_BATCH_BUDGET_MS,
      maxItemsPerBatch: HEAVY_DOM_MAX_SECTIONS_PER_BATCH,
      onTask: function (task) {
        if (!task || !task.sectionId) {
          return;
        }
        const domBuildStart = nowMs();
        reportPerf("heavy_section_dom_build", {
          requestId: requestId,
          detailId: runtime.currentDetailId,
          sectionId: task.sectionId,
          order: task.order,
          htmlLen: task.htmlLen,
          textChars: task.textChars
        });

        const sectionPayload = {
          label: task.label,
          html: task.html,
          isPlaceholder: false,
          order: task.order
        };
        const buildMs = Math.max(0, nowMs() - domBuildStart);

        const domInsertStart = nowMs();
        patchSection(task.sectionId, sectionPayload);
        const insertMs = Math.max(0, nowMs() - domInsertStart);

        reportPerf("heavy_section_dom_insert", {
          requestId: requestId,
          detailId: runtime.currentDetailId,
          sectionId: task.sectionId,
          order: task.order,
          htmlLen: task.htmlLen,
          buildMs: buildMs.toFixed(1),
          insertMs: insertMs.toFixed(1)
        });
      },
      onStale: function () {
        abortIfStale(requestId);
      },
      onDone: function () {
        if (abortIfStale(requestId) || isSessionStale(sessionId)) {
          return;
        }
        reportPerf("render_heavy_sections_done", {
          requestId: requestId,
          detailId: runtime.currentDetailId,
          sections: sections.length
        });
        renderKatexDeferred(requestId, sessionId, function () {
          completeRender(requestId, sessionId);
        });
      }
    });
  }

  function completeRender(requestId, sessionId) {
    if (abortIfStale(requestId) || isSessionStale(sessionId)) {
      return;
    }
    stopScheduler("lightPaint", "render_complete");
    const dom = cacheDomRefs();
    if (dom.root) {
      dom.root.classList.remove("is-rendering-light", "is-rendering-heavy", "is-stale-request");
      dom.root.classList.add("is-shell-ready");
      dom.root.dataset.requestId = String(requestId);
      dom.root.dataset.sessionId = String(sessionId || runtime.currentSessionId || 0);
    }
    reportPerf("render_complete", { requestId: requestId, detailId: runtime.currentDetailId });
  }

  function normalizePayload(rawPayload) {
    if (!rawPayload || typeof rawPayload !== "object") {
      return { state: "empty", requestId: 0, detailId: "", message: "" };
    }
    const normalized = Object.assign({}, rawPayload);
    normalized.state = String(normalized.state || "content").trim().toLowerCase();
    normalized.requestId = toNumber(normalized.requestId, 0);
    normalized.detailId = String(normalized.detailId || "").trim();
    return normalized;
  }

  function renderNonContent(payload) {
    stopAllSchedulers("render_non_content");
    runtime.currentSessionId += 1;
    runtime.currentRequestId = toNumber(payload.requestId, 0);
    runtime.currentDetailId = String(payload.detailId || "").trim();
    if (runtime.currentRequestId) {
      runtime.requestDetailMap.set(runtime.currentRequestId, runtime.currentDetailId);
    }
    runtime.requestStartedAtMs = nowMs();
    runtime.previousPhaseAtMs = runtime.requestStartedAtMs;

    reportPerf("render_request_received", { requestId: runtime.currentRequestId, detailId: runtime.currentDetailId });
    if (payload.state === "error") {
      setState("error", String(payload.message || "").trim());
    } else {
      setState("empty", String(payload.message || "").trim());
    }
    completeRender(runtime.currentRequestId || 0, runtime.currentSessionId);
  }

  function initShell() {
    const dom = cacheDomRefs();
    if (!dom.root || !dom.header || !dom.meta || !dom.core || !dom.condition || !dom.remarks || !dom.vars || !dom.sections) {
      return { ok: false, error: "detail_shell_dom_missing" };
    }
    if (!runtime.rootClickBound) {
      dom.root.addEventListener("click", function () {});
      runtime.rootClickBound = true;
    }
    runtime.shellInitialized = true;
    dom.root.classList.add("is-shell-ready");
    return { ok: true, shellReady: true };
  }

  function renderDetail(rawPayload) {
    const initResult = initShell();
    if (!initResult.ok) {
      return initResult;
    }

    const payload = normalizePayload(rawPayload);
    if (payload.state !== "content") {
      renderNonContent(payload);
      return {
        ok: true,
        accepted: true,
        requestId: payload.requestId,
        detailId: payload.detailId,
        state: payload.state
      };
    }

    const session = beginRenderSession(payload);
    if (!session.ok) {
      setState("error", "详情请求参数无效。");
      return { ok: false, accepted: false, error: session.error, requestId: session.requestId, detailId: session.detailId };
    }

    renderLightSections(payload);
    flushLightPaint(session.requestId, session.sessionId, function () {
      if (abortIfStale(session.requestId) || isSessionStale(session.sessionId)) {
        return;
      }
      const dom = cacheDomRefs();
      if (dom.root) {
        dom.root.classList.remove("is-rendering-light");
        dom.root.classList.add("is-rendering-heavy");
      }
      renderKatexVisibleFirst(session.requestId, session.sessionId);
      renderHeavySections(payload, session.sessionId);
    });

    return {
      ok: true,
      accepted: true,
      requestId: session.requestId,
      detailId: session.detailId,
      state: "content"
    };
  }

  function bootstrap() {
    initShell();
    renderDetail(window.__DETAIL_INITIAL_DATA__ || { state: "empty", message: "" });
  }

  window.DetailRuntime = {
    initShell: initShell,
    renderDetail: renderDetail,
    abortIfStale: abortIfStale,
    reportPerf: reportPerf
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", bootstrap, { once: true });
  } else {
    bootstrap();
  }
})();
