# PkgClip - Cached Packages Trimmer Utility

PkgClip is a little helper for Arch Linux users, that will scan your pacman cache directories and help you trim it a bit.

Whenever pacman downloads packages, it saves them in a cache directory, and never removes them. Over time, this cache can grow a lot.

While pacman does offer options to clean your cache, they are very basic. Its option `--clean` (`-c`) will remove packages that are no longer installed from the cache (as well as currently unused sync databases. Note that PkgClip does not touch databases, only packages.), or remove all packages if you specify it twice.

PkgClip aims at giving you a little more options. To do so, **it will classify all packages into different "groups"** (also called "reasons", as in reason for the recommended action), each group having a recommendation (Keep or Remove), used as default choice.

You will of course be able to then review it all, and decides which packages shall be removed, and which shall not.

## Want to know more?

Some useful links if you're looking for more info:

- [blog post about PkgClip](http://mywaytoarch.tumblr.com/post/16005116198/pkgclip-does-your-pacman-cache-need-a-trim "PkgClip: Does your pacman cache need a trim?")

- [source code & issue tracker](https://github.com/jjk-jacky/pkgclip "PkgClip @ GitHub.com")

- [PKGBUILD in AUR](https://aur.archlinux.org/packages.php?ID=55870 "AUR: pkgclip")

Plus, PkgClip comes with a man page.

