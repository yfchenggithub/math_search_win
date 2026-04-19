(function () {
  "use strict";

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
    const paragraphs = raw.split(/\n{2,}/).map((item) => item.trim()).filter(Boolean);
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
      errorText.textContent = message || "详情数据暂时不可用";
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
      const title = String(row.title || "").trim();
      const html = String(row.html || "").trim();
      const text = String(row.text || "").trim();
      return !!title && (!!html || !!text);
    });

    if (visibleRows.length === 0) {
      const emptyHint = document.createElement("p");
      emptyHint.className = "detail-state-text";
      emptyHint.textContent = "暂无更多内容。";
      container.appendChild(emptyHint);
      return;
    }

    visibleRows.forEach((row) => {
      const section = document.createElement("section");
      const kind = normalizeSectionKind(String(row.kind || "").trim().toLowerCase());
      let className = "detail-section";
      if (kind === "note") {
        className += " detail-section-note";
      } else if (kind === "pitfall") {
        className += " detail-section-pitfall";
      } else if (kind === "summary") {
        className += " detail-section-summary";
      }
      section.className = className;
      section.dataset.sectionKey = String(row.key || "").trim();

      const header = document.createElement("div");
      header.className = "detail-section-header";

      const title = document.createElement("h2");
      title.className = "detail-section-title";
      title.textContent = String(row.title || "").trim() || "内容";
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

      body.appendChild(rich);
      section.appendChild(header);
      section.appendChild(body);
      container.appendChild(section);

      renderMath(rich);
    });
  }

  function renderDetail(data) {
    if (!data || data.isValid === false) {
      showErrorState((data && data.errorMessage) || "详情数据暂时不可用");
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
    renderMath
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", bootstrap);
  } else {
    bootstrap();
  }
})();
