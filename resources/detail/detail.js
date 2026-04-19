(function () {
  "use strict";

  const SECTION_LABELS = Object.freeze({
    statement: "结论",
    intuition: "理解",
    derivation: "推导",
    proof: "证明",
    pitfalls: "易错点",
    usage: "用法",
    summary: "总结",
    notes: "备注"
  });

  const SECTION_CLASS_MAP = Object.freeze({
    statement: "detail-section-statement",
    intuition: "detail-section-intuition",
    derivation: "detail-section-derivation",
    proof: "detail-section-proof",
    pitfalls: "detail-section-pitfall",
    usage: "detail-section-usage",
    summary: "detail-section-summary",
    notes: "detail-section-note"
  });

  const COLLAPSIBLE_KEYS = new Set(["proof", "derivation"]);
  const COLLAPSE_CHAR_THRESHOLD = 280;
  const TOAST_DURATION_MS = 1400;
  let toastTimerId = 0;

  function byId(id) {
    return document.getElementById(id);
  }

  function escapeHtml(value) {
    const text = value == null ? "" : String(value);
    return text
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll("\"", "&quot;")
      .replaceAll("'", "&#39;");
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
    if (paragraphs.length === 0) {
      return "";
    }
    return paragraphs
      .map((item) => "<p>" + escapeHtml(item).replace(/\n/g, "<br/>") + "</p>")
      .join("");
  }

  function clearNode(node) {
    if (!node) {
      return;
    }
    while (node.firstChild) {
      node.removeChild(node.firstChild);
    }
  }

  function switchState(state, message) {
    const emptyState = byId("detailEmptyState");
    const errorState = byId("detailErrorState");
    const contentState = byId("detailContent");
    const errorText = byId("detailErrorText");
    const emptyText = byId("detailEmptyText");

    if (emptyState) {
      emptyState.hidden = state !== "empty";
    }
    if (errorState) {
      errorState.hidden = state !== "error";
    }
    if (contentState) {
      contentState.hidden = state !== "content";
    }

    if (state === "error" && errorText) {
      errorText.textContent = message || "详情数据暂时不可用。";
    }
    if (state === "empty" && emptyText && message) {
      emptyText.textContent = message;
    }
  }

  function showEmptyState(message) {
    switchState("empty", message);
  }

  function showErrorState(message) {
    switchState("error", message);
  }

  function normalizeSectionKind(kind) {
    if (kind === "note" || kind === "pitfall" || kind === "summary") {
      return kind;
    }
    return "normal";
  }

  function normalizeSectionKey(rawKey) {
    const key = String(rawKey || "").trim().toLowerCase();
    if (SECTION_CLASS_MAP[key]) {
      return key;
    }
    if (key === "pitfall") {
      return "pitfalls";
    }
    if (key === "note") {
      return "notes";
    }
    return "notes";
  }

  function resolveSectionTitle(row, sectionKey) {
    const explicitTitle = String((row && row.title) || "").trim();
    if (explicitTitle) {
      return explicitTitle;
    }
    return SECTION_LABELS[sectionKey] || SECTION_LABELS.notes;
  }

  function renderTags(tags) {
    const container = byId("detailTags");
    if (!container) {
      return;
    }
    clearNode(container);

    if (!Array.isArray(tags)) {
      return;
    }

    tags.forEach((rawTag) => {
      const tag = String(rawTag || "").trim();
      if (!tag) {
        return;
      }
      const chip = document.createElement("span");
      chip.className = "detail-tag-chip";
      chip.textContent = tag;
      container.appendChild(chip);
    });
  }

  function renderMath(rootElement) {
    if (!rootElement || typeof window.renderMathInElement !== "function") {
      return;
    }

    try {
      window.renderMathInElement(rootElement, {
        delimiters: [
          { left: "$$", right: "$$", display: true },
          { left: "\\[", right: "\\]", display: true },
          { left: "\\(", right: "\\)", display: false },
          { left: "$", right: "$", display: false }
        ],
        throwOnError: false
      });
    } catch (error) {
      console.warn("KaTeX render failed:", error);
    }
  }

  function ensureToast() {
    let toast = byId("detailActionToast");
    if (toast) {
      return toast;
    }
    toast = document.createElement("div");
    toast.id = "detailActionToast";
    toast.className = "detail-copy-toast";
    toast.setAttribute("role", "status");
    toast.setAttribute("aria-live", "polite");
    toast.hidden = true;
    document.body.appendChild(toast);
    return toast;
  }

  function showToast(message, tone) {
    const toast = ensureToast();
    if (!toast) {
      return;
    }
    toast.textContent = String(message || "").trim();
    toast.classList.remove("detail-copy-toast-success", "detail-copy-toast-error");
    toast.classList.add(tone === "error" ? "detail-copy-toast-error" : "detail-copy-toast-success");
    toast.hidden = false;

    if (toastTimerId) {
      window.clearTimeout(toastTimerId);
    }
    toastTimerId = window.setTimeout(() => {
      toast.hidden = true;
      toast.textContent = "";
    }, TOAST_DURATION_MS);
  }

  function buildSectionId(sectionKey, index) {
    return "detail-section-" + sectionKey + "-" + String(index + 1);
  }

  function extractReadableTextFromHtml(html) {
    const markup = String(html || "").trim();
    if (!markup) {
      return "";
    }
    const scratch = document.createElement("div");
    scratch.innerHTML = markup;
    return String(scratch.textContent || "").replace(/\u00a0/g, " ").trim();
  }

  function fallbackCopyText(text) {
    const value = String(text || "").trim();
    if (!value) {
      return false;
    }
    try {
      const helper = document.createElement("textarea");
      helper.value = value;
      helper.setAttribute("readonly", "readonly");
      helper.style.position = "fixed";
      helper.style.opacity = "0";
      helper.style.pointerEvents = "none";
      helper.style.left = "-9999px";
      document.body.appendChild(helper);
      helper.select();
      helper.setSelectionRange(0, helper.value.length);
      const copied = document.execCommand("copy");
      document.body.removeChild(helper);
      return copied;
    } catch (error) {
      return false;
    }
  }

  function writeTextToClipboard(text) {
    const value = String(text || "").trim();
    if (!value) {
      return Promise.resolve(false);
    }

    if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
      return navigator.clipboard
        .writeText(value)
        .then(() => true)
        .catch(() => fallbackCopyText(value));
    }
    return Promise.resolve(fallbackCopyText(value));
  }

  function composeSectionCopyText(title, content) {
    const safeTitle = String(title || "").trim();
    const safeContent = String(content || "").trim();
    if (safeTitle && safeContent) {
      return safeTitle + "\n\n" + safeContent;
    }
    return safeContent || safeTitle;
  }

  function shouldEnableCollapse(sectionKey, richNode) {
    if (!COLLAPSIBLE_KEYS.has(sectionKey) || !richNode) {
      return false;
    }
    const plainText = String(richNode.textContent || "").replace(/\s+/g, "");
    const blockCount = richNode.querySelectorAll("p, li, .detail-math-block, blockquote").length;
    return plainText.length >= COLLAPSE_CHAR_THRESHOLD || blockCount >= 8;
  }

  function setSectionExpanded(section, expanded) {
    if (!section) {
      return;
    }
    const body = section.querySelector(".detail-section-body");
    const toggleButton = section.querySelector(".detail-section-toggle");
    if (body) {
      body.hidden = !expanded;
    }
    section.classList.toggle("is-collapsed", !expanded);
    if (toggleButton) {
      toggleButton.textContent = expanded ? "收起" : "展开";
      toggleButton.setAttribute("aria-expanded", expanded ? "true" : "false");
    }
  }

  function toggleSection(section) {
    if (!section) {
      return;
    }
    const willExpand = section.classList.contains("is-collapsed");
    setSectionExpanded(section, willExpand);
  }

  function createActionButton(label, className, onClick) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "detail-section-action " + className;
    button.textContent = label;
    button.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();
      onClick();
    });
    return button;
  }

  function createActionLink(label, href, className, ariaLabel) {
    const link = document.createElement("a");
    link.className = "detail-section-action " + className;
    link.href = href;
    link.textContent = label;
    if (ariaLabel) {
      link.setAttribute("aria-label", ariaLabel);
    }
    return link;
  }

  function renderSections(sections) {
    const container = byId("detailSections");
    if (!container) {
      return;
    }

    clearNode(container);
    const rows = Array.isArray(sections) ? sections : [];

    const visibleRows = rows.filter((row) => {
      if (!row || row.visible === false) {
        return false;
      }
      const html = String(row.html || "").trim();
      const text = String(row.text || "").trim();
      return !!html || !!text;
    });

    if (visibleRows.length === 0) {
      const emptyHint = document.createElement("p");
      emptyHint.className = "detail-state-text";
      emptyHint.textContent = "暂无更多内容。";
      container.appendChild(emptyHint);
      return;
    }

    visibleRows.forEach((row, index) => {
      const section = document.createElement("section");
      const sectionKey = normalizeSectionKey(row.key);
      const sectionKind = normalizeSectionKind(String(row.kind || "").trim().toLowerCase());
      let sectionClass = SECTION_CLASS_MAP[sectionKey];
      if (sectionKind === "pitfall") {
        sectionClass = SECTION_CLASS_MAP.pitfalls;
      } else if (sectionKind === "summary") {
        sectionClass = SECTION_CLASS_MAP.summary;
      }

      section.className = "detail-section";
      if (sectionClass) {
        section.classList.add(sectionClass);
      }
      section.dataset.sectionKey = sectionKey;
      section.dataset.sectionKind = sectionKind;
      section.id = buildSectionId(sectionKey, index);

      const header = document.createElement("div");
      header.className = "detail-section-header";

      const title = document.createElement("h2");
      title.className = "detail-section-title";
      const sectionTitle = resolveSectionTitle(row, sectionKey);
      title.textContent = sectionTitle || "内容";
      header.appendChild(title);

      const body = document.createElement("div");
      body.className = "detail-section-body";

      const rich = document.createElement("div");
      rich.className = "detail-rich-text";
      const html = String(row.html || "").trim();
      const text = String(row.text || "").trim();
      if (html) {
        rich.innerHTML = html;
      } else {
        rich.innerHTML = htmlFromText(text);
      }

      const copyContent = text || extractReadableTextFromHtml(html);
      const sectionCopyText = composeSectionCopyText(sectionTitle, copyContent);

      const actions = document.createElement("div");
      actions.className = "detail-section-actions";

      const anchorLink = createActionLink("定位", "#" + section.id, "detail-section-anchor", "定位到" + sectionTitle);
      actions.appendChild(anchorLink);

      const copyButton = createActionButton("复制", "detail-section-copy", () => {
        writeTextToClipboard(sectionCopyText).then((copied) => {
          showToast(copied ? "已复制当前分区" : "复制失败，请手动复制", copied ? "success" : "error");
        });
      });
      actions.appendChild(copyButton);

      if (shouldEnableCollapse(sectionKey, rich)) {
        section.classList.add("is-collapsible");
        const toggleButton = createActionButton("收起", "detail-section-toggle", () => toggleSection(section));
        toggleButton.setAttribute("aria-expanded", "true");
        actions.appendChild(toggleButton);
      }

      header.appendChild(actions);
      body.appendChild(rich);
      section.appendChild(header);
      section.appendChild(body);
      container.appendChild(section);

      renderMath(rich);
      if (section.classList.contains("is-collapsible")) {
        setSectionExpanded(section, true);
      }
    });
  }

  function renderDetail(data) {
    if (!data || data.isValid === false) {
      showErrorState((data && data.errorMessage) || "详情数据暂时不可用。");
      return;
    }

    const title = byId("detailTitle");
    const conclusionId = byId("detailConclusionId");
    const module = byId("detailModule");
    const summaryBlock = byId("detailSummaryBlock");
    const summaryText = byId("detailSummaryText");

    if (title) {
      title.textContent = String(data.title || "").trim() || "未命名结论";
    }
    if (conclusionId) {
      conclusionId.textContent = String(data.conclusionId || "").trim();
    }
    if (module) {
      module.textContent = String(data.module || "").trim();
    }

    renderTags(data.tags);

    const summary = String(data.summary || "").trim();
    if (summaryBlock) {
      summaryBlock.hidden = !summary;
    }
    if (summaryText) {
      summaryText.textContent = summary;
      if (summary) {
        renderMath(summaryText);
      }
    }

    renderSections(data.sections);
    switchState("content");
  }

  function bootstrap() {
    const initial = window.__DETAIL_INITIAL_DATA__;
    if (!initial || typeof initial !== "object") {
      showEmptyState();
      return;
    }

    const state = String(initial.state || "").trim().toLowerCase();
    if (state === "error") {
      showErrorState(String(initial.message || "").trim());
      return;
    }
    if (state === "content") {
      renderDetail(initial.detail || null);
      return;
    }

    showEmptyState(String(initial.message || "").trim());
  }

  window.DetailPage = {
    showEmptyState,
    showErrorState,
    renderDetail,
    renderTags,
    renderSections,
    renderMath,
    toggleSection
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", bootstrap);
  } else {
    bootstrap();
  }
})();
