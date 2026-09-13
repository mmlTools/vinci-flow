# OBS Flatpak extension

VinciFlow is packaged as `com.obsproject.Studio.Plugin.VinciFlow`, an extension
of `com.obsproject.Studio//stable`, for Linux x86_64. It builds from source with
`org.freedesktop.Sdk//25.08` and OBS's own libraries and Qt. This follows the
[OBS packaging guidance](https://github.com/obsproject/obs-studio/discussions/11956)
and the [Flatpak extension layout](https://docs.flatpak.org/en/latest/extension.html).

The build installs into `/app/plugins/VinciFlow`, with the module in
`lib/obs-plugins` and resources in `share/obs/obs-plugins/vinci-flow`. The extension
inherits OBS's sandbox permissions. It does not request extra host access.

## Install a bundle

Download the `vinci-flow-<version>-flatpak-x86_64.flatpak` artifact from a successful
CI build or release, close OBS, then run:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub com.obsproject.Studio//stable
flatpak install --user ./vinci-flow-<version>-flatpak-x86_64.flatpak
flatpak run com.obsproject.Studio
```

Replace `<version>` with the downloaded version. Use the same Flatpak installation
scope for OBS and the extension; the example uses a per-user installation.
This is a downloadable extension bundle, not a published Flathub listing. The
Ubuntu `.deb` and native Linux `.so` packages are built for a different environment
and should not be copied into the Flatpak application's config directory.

## Build locally on Linux

Install `flatpak` and `flatpak-builder` using your distribution's package manager.
From the repository root:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub com.obsproject.Studio//stable org.freedesktop.Sdk//25.08
flatpak-builder --user --install-deps-from=flathub --force-clean \
  --repo=flatpak-repo build_flatpak \
  build-aux/flatpak/com.obsproject.Studio.Plugin.VinciFlow.json
flatpak build-bundle --runtime flatpak-repo vinci-flow-flatpak-x86_64.flatpak \
  com.obsproject.Studio.Plugin.VinciFlow stable
flatpak install --user ./vinci-flow-flatpak-x86_64.flatpak
```

The manifest builds the current working tree. Keep unrelated build outputs outside
the checkout or add them to its source `skip` list. CI uses the same manifest and
adds the bundle to release artifacts, checksums, and the combined release ZIP.
Update the SDK branch when OBS changes its Flatpak SDK; native Ubuntu builds are
not a substitute for this build.

## Graphics and file access

On first use in Flatpak, if no output folder has been configured, VinciFlow creates
`resources` under its OBS module configuration directory. On the host this is
normally:

```text
~/.var/app/com.obsproject.Studio/config/obs-studio/plugin_config/vinci-flow/resources/
```

Existing configured folders are preserved. The dock shows the actual path and lets
you choose another folder through the file chooser. Folder selection checks write
access before replacing the current path. If access fails, choose a folder OBS can
write to. Keep `lt.html`, CSS, JavaScript, JSON, and media together; the Browser Source
uses relative paths to these files. The installed `/app/plugins` directory is
read-only and is not an output folder.

Host automation can write parameter JSON files in the resources folder, or use the
OBS WebSocket vendor API. For template editing, use the built-in editor or System
default through the desktop portal. A custom host executable may not be available
inside the sandbox.

## Validation

The manifest checks that the module and locale were installed in the extension
layout and that `ldd` reports no missing libraries. After a successful Linux build,
verify in Flatpak OBS that the VinciFlow dock loads, a first-run resources folder is
created, title show/hide works, imported images and sounds load, and graphics survive
rapid scene switching and an OBS restart. Check the OBS log for plugin load errors.
Also test choosing an inaccessible output folder and confirm the previous folder
and graphics remain selected.
