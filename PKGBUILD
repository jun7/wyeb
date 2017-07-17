# Maintainer: jun7 <jun7@hush.com>

pkgname=wyeb
pkgver=1.1
pkgrel=1
branch=master
pkgdesc="A vim-like keybind webkit2gtk browser which focused to be simple."
arch=('x86_64')
conflicts=('wyeb')
provides=('wyeb')
license=('GPL')
url="https://github.com/jun7/wyeb"
depends=('webkit2gtk' 'markdown' 'perl-file-mimeinfo')
makedepends=('git')
source=("git://github.com/jun7/wyeb.git#branch=$branch")
md5sums=('SKIP')

pkgver(){
	cd "$srcdir/wyeb"
	printf "$pkgver.%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

prepare() {
	cd "$srcdir/wyeb"
	git pull --rebase origin $branch
}

build() {
	cd "$srcdir/wyeb"
	make
}

package() {
	cd "$srcdir/wyeb"
	install -Dm755 wyeb   "$pkgdir/usr/bin/wyeb"
	install -Dm755 ext.so   "$pkgdir/usr/share/wyebrowser/ext.so"
	install -Dm644 wyeb.png   "$pkgdir/usr/share/pixmaps/wyeb.png"
	install -Dm644 wyeb.desktop "$pkgdir/usr/share/applications/wyeb.desktop"
}
