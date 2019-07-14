use Test::More tests => 6 + 3 + 2;

require(get_rinclude_path('FSE'));

my $installed = [
	compose_installed_record('a', 1) . "X-Marks: xx, yy, ww\n",
	compose_installed_record('b', 2),
	compose_installed_record('c', 3) . "X-Marks: yy (zz)\n",
];
my $cupt = setup('dpkg_status' => $installed);

eis($cupt, 'field(X-Marks, .*)', qw(a b c));
eis($cupt, 'field(X-Marks, /.*/)', qw(a b c));
eis($cupt, 'field(X-Marks, .+)', qw(a c));
eis($cupt, 'field(X-Marks, /.+/)', qw(a c));
eis($cupt, 'field(X-Marks, xx.*)', qw(a));
eis($cupt, 'field(X-Marks, /xx.*/)', qw(a));

eis($cupt, 'field(X-Marks, /xx, yy.*/)', qw(a));
eis($cupt, 'field(X-Marks, /.*yy, ww/)', qw(a));
eis($cupt, 'field(X-Marks, /.*xx, ww.*/)', qw());

eis($cupt, 'field(X-Marks, /yy (.*)/)', qw(c));
eis($cupt, 'field(X-Marks, /zz (.*)/)', qw());

