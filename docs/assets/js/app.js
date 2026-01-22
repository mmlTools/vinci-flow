(function () {
  const routes = {
    home: { title: "Home", file: "pages/home.html", subtitle: "Overview of vinci-flow (VinciFlow)" },
    install: { title: "Install & Setup", file: "pages/install.html", subtitle: "OBS integration and output workflow" },
    themes: { title: "Building Your Own Themes", file: "pages/themes.html", subtitle: "Template authoring: HTML/CSS/JS + placeholders" },
    "runtime-params": { title: "Runtime Parameters", file: "pages/api.html", subtitle: "API Template + JSON-driven updates (no animation interruption)" },
    websocket: { title: "Setup WebSocket", file: "pages/websocket.html", subtitle: "Automation via obs-websocket vendor requests" },
  };

  const pillMap = {
    home: ["Lower Thirds", "Groups", "Hotkeys", "Output HTML"],
    install: ["OBS", "Browser Source", "Output Directory", "Troubleshooting"],
    themes: ["Templates", "Placeholders", "CSS/JS", "Animate.css"],
    "runtime-params": ["API Template", "parameters.json", "Async polling", "data-* bindings"],
    websocket: ["obs-websocket v5", "Vendor Requests", "Events", "Automation"],
  };

  function getRoute() {
    const hash = (location.hash || "#home").replace("#", "");
    return routes[hash] ? hash : "home";
  }

  function setActiveNav(routeKey) {
    document.querySelectorAll("[data-nav]").forEach((a) => {
      a.classList.toggle("active", a.getAttribute("href") === "#" + routeKey);
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
    if (mainSubtitle) mainSubtitle.textContent = route.subtitle || "Documentation for vinci-flow (VinciFlow)";

    renderPills(routeKey);

    try {
      const res = await fetch(route.file, { cache: "no-store" });
      if (!res.ok) throw new Error("HTTP " + res.status);

      const html = await res.text();
      if (content) content.innerHTML = html;

      document.title = route.title + " · vinci-flow docs";
      window.scrollTo({ top: 0, behavior: "instant" });
    } catch (err) {
      if (content) {
        content.innerHTML =
          `<div class="callout warn"><b>Could not load this page.</b>` +
          `<div style="margin-top:6px; color: rgba(255,255,255,.78);">` +
          `Check that <code>${route.file}</code> exists and is being served by your web server.` +
          `</div></div>`;
      }
      console.error(err);
    }
  }

  window.addEventListener("hashchange", () => load(getRoute()));
  document.addEventListener("DOMContentLoaded", () => load(getRoute()));
})();
