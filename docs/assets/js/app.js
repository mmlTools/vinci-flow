(function(){
  const routes = {
    home: { title: "Home", file: "pages/home.html" },
    install: { title: "Install & Setup", file: "pages/install.html" },
    themes: { title: "Building Your Own Themes", file: "pages/themes.html" },
    websocket: { title: "Setup WebSocket", file: "pages/websocket.html" },
  };

  function getRoute(){
    const hash = (location.hash || "#home").replace("#","");
    return routes[hash] ? hash : "home";
  }

  async function load(routeKey){
    const route = routes[routeKey];
    const mainTitle = document.querySelector("[data-page-title]");
    const mainSubtitle = document.querySelector("[data-page-subtitle]");
    const content = document.querySelector("#docContent");
    const pills = document.querySelector("#docPills");

    document.querySelectorAll("[data-nav]").forEach(a=>{
      a.classList.toggle("active", a.getAttribute("href")==="#"+routeKey);
    });

    mainTitle.textContent = route.title;
    mainSubtitle.textContent = "Documentation for vinci-flow (Smart Lower Thirds)";

    // Pills per page
    const pillMap = {
      home: ["Lower Thirds", "Groups", "Hotkeys", "Output HTML"],
      install: ["OBS", "Browser Source", "Output Directory", "Troubleshooting"],
      themes: ["Templates", "Placeholders", "CSS/JS", "Animate.css"],
      websocket: ["obs-websocket v5", "Vendor Requests", "Events", "Automation"],
    };
    pills.innerHTML = "";
    (pillMap[routeKey] || []).forEach(t=>{
      const span = document.createElement("span");
      span.className = "pill";
      span.textContent = t;
      pills.appendChild(span);
    });

    try{
      const res = await fetch(route.file, {cache:"no-store"});
      if(!res.ok) throw new Error("HTTP "+res.status);
      const html = await res.text();
      content.innerHTML = html;
      // update document title
      document.title = route.title + " · vinci-flow docs";
      // scroll to top on navigation
      window.scrollTo({top:0, behavior:"instant"});
    }catch(err){
      content.innerHTML = `<div class="callout warn"><b>Could not load this page.</b><div style="margin-top:6px; color: rgba(255,255,255,.78);">Check that <code>${route.file}</code> exists and is being served by your web server.</div></div>`;
      console.error(err);
    }
  }

  window.addEventListener("hashchange", ()=>load(getRoute()));
  document.addEventListener("DOMContentLoaded", ()=>load(getRoute()));
})();
