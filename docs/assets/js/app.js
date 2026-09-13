(function () {
  const routes = {
    overview: {
      title: "Overview",
      file: "pages/home.html",
      subtitle: "What vinci-flow is and how the lower-thirds workflow works.",
    },
    install: {
      title: "Install & Setup",
      file: "pages/install.html",
      subtitle: "OBS integration, the output directory, and first-run checklist.",
    },
    themes: {
      title: "Building Your Own Themes",
      file: "pages/themes.html",
      subtitle: "Author HTML/CSS/JS templates with placeholders and animations.",
    },
    "runtime-params": {
      title: "Runtime Parameters",
      file: "pages/api.html",
      subtitle: "API Template + JSON-driven updates without interrupting animations.",
    },
    websocket: {
      title: "Setup WebSocket",
      file: "pages/websocket.html",
      subtitle: "Automate vinci-flow via obs-websocket vendor requests and events.",
    },
  };

  const pillMap = {
    overview: ["Lower Thirds", "Groups", "Hotkeys", "Output HTML"],
    install: ["OBS", "Browser Source", "Output Directory", "Troubleshooting"],
    themes: ["Templates", "Placeholders", "CSS/JS", "Animate.css"],
    "runtime-params": ["API Template", "parameters.json", "Async polling", "data-* bindings"],
    websocket: ["obs-websocket v5", "Vendor Requests", "Events", "Automation"],
  };

  function getRoute() {
    const hash = (location.hash || "#overview").replace("#", "");
    return routes[hash] ? hash : "overview";
  }

  function setActiveNav(routeKey) {
    document.querySelectorAll(".nav__link[data-nav]").forEach((a) => {
      a.classList.toggle("is-active", a.getAttribute("href") === "#" + routeKey);
    });
  }

  function renderPills(routeKey) {
    const pills = document.querySelector("#docPills");
    if (!pills) return;
    pills.innerHTML = "";

    (pillMap[routeKey] || []).forEach((t) => {
      const span = document.createElement("span");
      span.className = "pill";
      span.textContent = t;
      pills.appendChild(span);
    });
  }

  async function load(routeKey) {
    const route = routes[routeKey];

    const mainTitle = document.querySelector("[data-page-title]");
    const mainSubtitle = document.querySelector("[data-page-subtitle]");
    const content = document.querySelector("#docContent");

    setActiveNav(routeKey);

    if (mainTitle) mainTitle.textContent = route.title;
    if (mainSubtitle) mainSubtitle.textContent = route.subtitle || "";

    renderPills(routeKey);

    try {
      const res = await fetch(route.file, { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);

      const html = await res.text();
      if (content) content.innerHTML = html;

      document.title = route.title + " · vinci-flow docs";
      window.scrollTo({ top: 0, behavior: "instant" });

      addCopyButtons();
    } catch (err) {
      if (content) {
        content.innerHTML =
          `<div class="warn"><strong>Could not load this page.</strong>` +
          `<div style="margin-top:6px;">Check that <code>${route.file}</code> exists and is being served by your host.</div></div>`;
      }
      console.error(err);
    }
  }

  // Mobile toggle
  const sidebar = document.getElementById("sidebar");
  const toggle = document.getElementById("sidebarToggle");
  if (toggle && sidebar) {
    toggle.addEventListener("click", () => {
      const open = sidebar.classList.toggle("is-open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }

  // Navigation search filter
  const navSearch = document.getElementById("navSearch");
  if (navSearch) {
    navSearch.addEventListener("input", () => {
      const q = navSearch.value.trim().toLowerCase();
      const links = Array.from(document.querySelectorAll(".nav__link"));
      links.forEach((a) => {
        const text = (a.textContent || "").toLowerCase();
        a.style.display = !q || text.includes(q) ? "" : "none";
      });

      const sections = Array.from(document.querySelectorAll(".nav__section"));
      sections.forEach((sec) => {
        let el = sec.nextElementSibling;
        let any = false;
        while (el && !el.classList.contains("nav__section")) {
          if (el.classList.contains("nav__link") && el.style.display !== "none") any = true;
          el = el.nextElementSibling;
        }
        sec.style.display = any || !q ? "" : "none";
      });
    });
  }

  // Copy buttons for code blocks
  function addCopyButtons() {
    const pres = document.querySelectorAll("pre");
    pres.forEach((pre) => {
      if (pre.querySelector(".copy-btn")) return;

      const btn = document.createElement("button");
      btn.className = "copy-btn";
      btn.type = "button";
      btn.textContent = "Copy";
      btn.addEventListener("click", async () => {
        const code = pre.querySelector("code");
        const text = code ? code.innerText : pre.innerText;
        try {
          await navigator.clipboard.writeText(text);
          btn.textContent = "Copied";
          setTimeout(() => (btn.textContent = "Copy"), 1200);
        } catch (e) {
          btn.textContent = "Failed";
          setTimeout(() => (btn.textContent = "Copy"), 1200);
        }
      });

      pre.appendChild(btn);
    });
  }
  window.addCopyButtons = addCopyButtons;

  window.addEventListener("hashchange", () => load(getRoute()));
  document.addEventListener("DOMContentLoaded", () => load(getRoute()));
})();
