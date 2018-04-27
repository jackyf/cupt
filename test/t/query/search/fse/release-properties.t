use Test::More tests => 2*4 + 6 + 3;

require(get_rinclude_path('FSE'));

my $cupt = setup(
	'releases' => [
		{
			'hostname' => 's1.some.org',
			'archive' => 'a1',
			'codename' => 'c1',
			'version' => 'v1',
			'vendor' => 'd1',
			'components' => [
				{ 'component' => 'p1', 'packages' => [ compose_package_record('pkgx', 0) ] },
			],
		}, {
			'hostname' => 's2.other.org/nnn',
			'archive' => 'a2',
			'codename' => 'c2',
			'version' => 'v2',
			'vendor' => 'd2',
			'components' => [
				{ 'component' => 'p2', 'packages' => [ compose_package_record('pkgy', 1) ] },
				{ 'component' => 'p3', 'packages' => [ compose_package_record('pkgz', 2) ] },
			],
		}
	]
);

eis($cupt, 'release:archive(a1)' => qw(pkgx));
eis($cupt, 'Ra(a2)' => qw(pkgy pkgz));

eis($cupt, 'release:codename(c2)' => qw(pkgy pkgz));
eis($cupt, 'Rn(c1)' => qw(pkgx));

eis($cupt, 'release:version(v1)' => qw(pkgx));
eis($cupt, 'Rv(v2)' => qw(pkgy pkgz));

eis($cupt, 'release:vendor(d1)' => qw(pkgx));
eis($cupt, 'release:vendor(d.*)' => qw(pkgx pkgy pkgz));

eis($cupt, 'release:origin(other)' => qw());
eis($cupt, 'release:origin(.*other.*)' => qw(pkgy pkgz));
eis($cupt, 'release:origin(.*\.org.*)' => qw(pkgx pkgy pkgz));
eis($cupt, 'Ru(.*nnn)' => qw(pkgy pkgz));
eis($cupt, 'Ru(.*1.*)' => qw(pkgx));
eis($cupt, 'Ru(.*3.*)' => qw());

eis($cupt, 'release:component(p1)' => qw(pkgx));
eis($cupt, 'release:component(p2)' => qw(pkgy));
eis($cupt, 'Rc(p3)' => qw(pkgz));

