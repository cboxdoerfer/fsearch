## Making a release

- Update release notes in `NEWS`
- Update version number in `meson.build`
- Update version number in `data/io.github.cboxdoerfer.FSearch.appdata.xml.in`
- Update release notes in `data/io.github.cboxdoerfer.FSearch.appdata.xml.in`
- Update screenshots (if necessary) in `data/io.github.cboxdoerfer.FSearch.appdata.xml.in`
- Update version number in `copr/fsearch_release.spec`
- Update debian changelog: `dch --newrelease $version && dch --release`
- Build the project and make sure tests pass: `ninja -C $builddir test`
- Commit release: `git commit -a -m "Release FSearch $version"`
- Add tag: `git tag $version`
- Push changes and tag: `git push origin && git push origin $version`
- Create release on GitHub
- if major/minor release perform those steps now
- Update `io.github.cboxdoerfer.FSearch.yml` in flathub repository
- Make sure flatpak
  works: `flatpak-builder --force-clean --user --install builder-dir io.github.cboxdoerfer.FSearch.yml`
- Update `PKGBUILD` in *fsearch* AUR repository: `$EDITOR PKGBUILD && makepkg --printsrcinfo > .SRCINFO`
- Update OBS build files: `fsearch.dsc`, `fsearch.spec`, add `fsearch-$version.tar.gz` with stripped `debian` diretory
- Commit to OBS: `osc vc && osc commit`

### Major/Minor release

- Create a new branch: `git checkout -b "fsearch_$version" && git push -u origin fsearch_$version`
- Point Release PPA [recipe](https://code.launchpad.net/~christian-boxdoerfer/+recipe/fsearch-stable/+edit) to new
  branch
- Point copr [recipe](https://copr.fedorainfracloud.org/coprs/cboxdoerfer/fsearch/package/fsearch/edit) to new branch
