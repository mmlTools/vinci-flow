# Browser lifecycle regressions

Configure this standalone test project with the same libobs and Qt6 development
packages used for the plugin, and Node.js on PATH:

```sh
cmake -S tests -B build-tests -DCMAKE_PREFIX_PATH="<OBS prefix>;<Qt6 prefix>"
cmake --build build-tests --config RelWithDebInfo
ctest --test-dir build-tests -C RelWithDebInfo --output-on-failure
```

On Windows, include the OBS, obs-deps, and Qt6 DLL directories in PATH when
running CTest. The native test registers a fake browser with real libobs; it
does not launch CEF or initialize a graphics device. The JavaScript tests run
the complete generated `lt.js` with a controlled DOM, timers, and fetches.

These tests cover CSS migration and persistence, failed CSS writes, reload
counts, resizing, rebinding, DOM readiness, repeated initialization, visibility
transitions, and overlapping/failed visibility reads.

## Manual OBS verification

1. Start with an existing VinciFlow resources directory and browser source.
   Enable both **Shutdown source when not visible** and **Refresh browser when
   scene becomes active**, and set recognizable custom CSS.
2. Restart OBS with the updated plugin. Confirm the existing overlay is
   regenerated, both options are disabled, and the CSS appearance is preserved.
   VinciFlow moves the source's Custom CSS into `lt-browser.css` and retains a
   per-source backup in `vflow_custom_css`. The Custom CSS field becomes empty
   to bypass [OBS's OnLoadEnd injection](https://github.com/obsproject/obs-browser/blob/master/browser-client.cpp).
   For later styling changes, use VinciFlow's template CSS, or enter replacement
   source CSS and trigger a VinciFlow rebuild; `/* no overrides */` replaces an
   existing source CSS backup with an empty stylesheet.
3. Add the same source with **Add Existing** to four scenes. Switch rapidly
   between them, including a scene without the source, using Cut and Fade.
   Repeat in Studio Mode. Check responsiveness, graphics, output, and OBS logs
   for `obsCSS` or `appendChild` errors.
4. Show/hide titles, play a group, update API parameters, edit a template, and
   resize the source. Confirm graphics and audio cues still work and edits
   appear without a second manual refresh.
5. Restart OBS and switch scene collections with different selected browser
   sources. Confirm the bindings and each source's custom CSS survive.

The automated suite cannot establish whether a Windows compositor/GPU freeze
is resolved; the reporter's OBS/CEF/hardware combination requires this manual
scene-switching check. The console errors identify an upstream CSS injection
problem but do not by themselves establish the cause of that freeze.
