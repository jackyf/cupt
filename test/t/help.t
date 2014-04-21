use TestCupt;
use Test::More tests => 3;

my $cupt = TestCupt::setup();
my $cmd = "$cupt help";

is(exitcode($cmd), 0, "'help' command doesn't fail");

sub stderr { return `$_[0] 2>&1 >/dev/null`; }
is(stderr($cmd), '', "'help' doesn't error or warn'");

like(`$cmd`, qr/^Usage/, "'help' command prints an usage");

