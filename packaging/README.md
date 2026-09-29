# Packaging coole

`coole` is the package name.  It installs one program, `/usr/bin/coole`, with
its data in `/usr/share/coole`, its system configuration in `/etc/coole` and
its runtime plugin in `/usr/lib/coole` (`/usr/lib64/coole` on Fedora).  No file
is shared with the distribution `mc` package, so the two can be installed side
by side.

Two packages come out of it: `coole`, the editor, and `coole-lua` with the Lua
runtime plugin and the editor scripts.  The ctags, etags and spell plugins are
built into the editor, so there is no separate plugins package.  Gentoo builds
one package and puts Lua behind the `lua` USE flag.

## Nothing to edit for a release

No recipe here names a version.  The version comes from the release tag, which
is named `coole-X.Y.Z`, and `packaging/prepare.sh` writes the files that have to
name one:

| Generated | From |
| --- | --- |
| `debian/changelog` | the `coole-*` tags |
| `packaging/rpm/coole.spec` | `coole.spec.in` and the `coole-*` tags |
| `packaging/arch/PKGBUILD` | `PKGBUILD.in` |
| `packaging/gentoo/coole-<version>.ebuild` | `coole.ebuild.in` |

All four are in `.gitignore`.  Making a release is therefore: tag, and let the
`release` workflow build.  Nothing is committed afterwards.

Both changelogs take their entries from the section of `CHANGELOG.md` for the
version, and from the **annotated tag message** when there is no such section;
their dates come from the tag, so the same tag always yields the same source
package:

```sh
git tag -a coole-X.Y.Z -m "coole X.Y.Z"
```

Sign it with `-s` instead if you keep a signing key; the packaging reads the
message and the date, and depends on no signature.

A version with no tag of its own is a test build.  Its Debian entry is marked
`UNRELEASED`, which stops `dput` from taking it by accident.

Only release versions are accepted, `1.0.0` and the like.  A pre-release has to
sort *below* the release it leads to, and the only character that does that is a
tilde, which Git does not allow in a tag name.  Mapping `1.0.0-rc1` to
`1.0.0~rc1` would also mean renaming the orig tarball and its directory to
match, and Arch allows neither the tilde nor the hyphen in `pkgver`.  Until that
is built, `prepare.sh` refuses such a version rather than quietly producing a
package that outranks the release.  To rehearse the release workflow, run it
from Actions against a tag that already exists.

Do not replace the exact version dependency of `coole-lua` on `coole` with
`>=`: the runtime plugin ABI is not stable.

## Release source

`packaging/release-source.sh` creates the archive that every recipe consumes,
and that is uploaded as the GitHub release asset `coole-<version>.tar.gz`:

```sh
packaging/release-source.sh X.Y.Z coole-X.Y.Z
```

The archive is the tree of the tag as `git archive` gives it, with one file
added: `.tarball-version`, so that a build outside a Git checkout reports the
correct version.  It carries no generated build files: every recipe runs meson
on it.  The same tag always gives the same bytes, and the script refuses to
overwrite an existing archive.

`debian/` and `packaging/` carry `export-ignore` in `.gitattributes` and are
absent from the archive: the recipes are used from the repository, and a Debian
orig tarball must not carry a `debian` directory of its own.

## Architectures

Debian, Ubuntu and Fedora are built for `amd64` and `arm64`, each on a runner of
its own architecture.  The containers are multi-architecture, so both run the
same recipe; emulation is far too slow for a full build.  The two produce the
same source package and source RPM, and only one copy of each is attached to the
release.

Arch stays `x86_64`.  Arch Linux has no aarch64 port, and the `archlinux` image
is published for `x86_64` alone; the `PKGBUILD` still names `aarch64` for Arch
Linux ARM, which builds from its own recipes.  macOS is built for Apple Silicon.

A PPA builds its own binaries, so the architectures it produces are set in the
Launchpad settings of the PPA and not here.

## Building by hand

The `release` workflow does all of this on a tag push, in a container per
distribution.  Locally the same steps check that a distribution can still build
the package.

Debian and Ubuntu.  The source package is built from the release archive with
`debian/` copied into it, not from the Git checkout: that keeps it free of
patches, since the archive carries `.tarball-version` and no `debian` directory.

```sh
packaging/release-source.sh X.Y.Z coole-X.Y.Z
packaging/prepare.sh X.Y.Z dist/coole-X.Y.Z.tar.gz
tar -xf dist/coole-X.Y.Z.tar.gz -C /tmp
cp dist/coole-X.Y.Z.tar.gz /tmp/coole_X.Y.Z.orig.tar.gz
cp -r debian /tmp/coole-X.Y.Z/
cd /tmp/coole-X.Y.Z && dpkg-buildpackage -us -uc
```

`sudo apt install ../coole_*.deb ../coole-lua_*.deb` installs the result; use
`apt install ./file.deb`, never `dpkg -i`.  For a PPA, set the target series and
a series suffix:

```sh
DEB_DISTRIBUTION=noble DEB_VERSION_SUFFIX='~ubuntu24.04.1' \
    packaging/prepare.sh X.Y.Z dist/coole-X.Y.Z.tar.gz
```

## Launchpad PPA

A PPA builds the binaries itself, so what it takes is a signed **source**
package per Ubuntu series.  `packaging/ppa-source.sh` builds them:

```sh
packaging/release-source.sh X.Y.Z coole-X.Y.Z
packaging/ppa-source.sh X.Y.Z dist/coole-X.Y.Z.tar.gz resolute:26.04 noble:24.04 jammy:22.04
UPLOAD=yes PPA=ppa:OWNER/NAME packaging/ppa-source.sh X.Y.Z dist/coole-X.Y.Z.tar.gz noble:24.04
```

`NOSIGN=yes` builds unsigned packages for a dry run, `SIGN_KEY` picks the key,
`PPA` the target, which an upload has to name.

`PPA_REVISION` is the number after the series, `1` by default.  A PPA keeps
every version it has ever accepted, so an upload that was rejected or turned out
broken comes back as `~ubuntu24.04.2`; the same version is never accepted twice.

Each series is named with its Ubuntu version, because that version is what
orders the uploads: `~ubuntu24.04.1` sorts above `~ubuntu22.04.1`, so moving to
a newer series is an upgrade.  Codenames cannot do that, having wrapped the
alphabet in 2017.

The series lives in the Debian revision, not in the upstream version, so all
series of a release share one `coole_<version>.orig.tar.gz`.  Only the first
upload carries it; the rest refer to the one already in the PPA.  Use one
archive for every series of a release, the published one: Launchpad compares
what a later upload refers to against the copy it already has.

Before the first upload: register the OpenPGP key with Launchpad and confirm it
through the encrypted mail it sends, then create the PPA.  `debhelper-compat
(= 13)` needs the series to be 22.04 or newer.

RPM.  Put the archive in the RPM source directory:

```sh
packaging/prepare.sh X.Y.Z dist/coole-X.Y.Z.tar.gz
cp dist/coole-X.Y.Z.tar.gz ~/rpmbuild/SOURCES/
rpmbuild -ba packaging/rpm/coole.spec
```

For a downloaded RPM use `dnf install ./coole-*.rpm`, never `rpm -i`.

Arch.  `makepkg` finds the archive next to the `PKGBUILD` instead of
downloading it:

```sh
packaging/prepare.sh X.Y.Z dist/coole-X.Y.Z.tar.gz
cp dist/coole-X.Y.Z.tar.gz packaging/arch/
cd packaging/arch && makepkg -si
```

Gentoo.  Copy `packaging/gentoo` into a personal overlay as `app-editors/coole`,
together with the generated ebuild, and run `ebuild ... manifest`.  It is a
single package; Lua, gpm, X11 and the screen library are USE flags:

```sh
emerge app-editors/coole
```

Gentoo is the one distribution the workflow does not build: that needs a full
stage3 with a Portage snapshot, which costs more than the ebuild is worth.  It
stays a manual check.
