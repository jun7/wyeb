# Maintainer: jun7 <jun7@hush.com>
pkgname=wyeb-git
pkgver=1.1
pkgrel=3
pkgdesc="A vim-like webkit2gtk browser"
arch=('x86_64')
url="http://wyeb.org/"
license=('GPL3')
depends=('webkit2gtk-4.1' 'discount' 'perl-file-mimeinfo')
makedepends=('git')
_branch=4.1
source=("git+https://github.com/jun7/wyeb.git#branch=$_branch")
md5sums=('SKIP')

pkgver(){
	cd "$srcdir/wyeb"
	printf "%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

prepare() {
	cd "$srcdir/wyeb"
	git pull --rebase origin $_branch
	make clean
}

build() {
	cd "$srcdir/wyeb"
	DEBUG=0 make
}

package() {
	cd "$srcdir/wyeb"
	PREFIX="$pkgdir/usr" make install
}
