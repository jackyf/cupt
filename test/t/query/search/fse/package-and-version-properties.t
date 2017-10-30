use Test::More tests => 6 + 15;

require(get_rinclude_path('FSE'));

my @installed;
push @installed, compose_installed_record('i', '1') . <<END;
Priority: extra
Maintainer: One Two
Special-property-field: Special String
END
push @installed, compose_installed_record('a', '3') . <<'END';
Priority: standard
Section: doc
Maintainer: One Three <one.three@mail.tld>
END

my $cupt = setup(
	'dpkg_status' => \@installed,
	'packages' => [ compose_package_record('p', '2') ],
	'extended_states' => compose_autoinstalled_record('a')
);

eis($cupt, 'package:name(a)', qw(a));
eis($cupt, 'Pn(a)', qw(a));
eis($cupt, 'package:installed()', qw(i a));
eis($cupt, 'Pi()', qw(i a));
eis($cupt, 'package:automatically-installed()', qw(a));
eis($cupt, 'Pai()', qw(a));

eis($cupt, 'version(3.*)', qw(a));
eis($cupt, 'v(3.*)', qw(a));
eis($cupt, 'trusted', qw(p));
eis($cupt, 't', qw(p));
eis($cupt, 'priority(extra)', qw(i p));
eis($cupt, 'p(extra)', qw(i p));
eis($cupt, 'priority(standard)', qw(a));
eis($cupt, 'maintainer(/One.*/)', qw(a i));
eis($cupt, 'm(/One.*/)', qw(a i));
eis($cupt, 'maintainer(.*mail.*)', qw(a));
eis($cupt, 'section(//)', qw(i p));
eis($cupt, 's(//)', qw(i p));
eis($cupt, 'section(doc)', qw(a));
eis($cupt, 'field(Special-property-field, /.+/)', qw(i));
eis($cupt, 'f(Special-property-field, /.+/)', qw(i));

