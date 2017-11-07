use Test::More tests => 3+3+3+2+1+3+4;

require(get_rinclude_path('FSE'));

my @installed;
push @installed, compose_installed_record('xx', '1') . <<END;
Source: nn (0.8)
Priority: extra
Maintainer: One Two
Special-property-field: Special String
END
push @installed, compose_installed_record('yy', '1') . <<'END';
Provides: mm, pp (= 3)
Essential: yes
Description: line 1
 Line 2 ioqwofsdfdasdfsdf.
 Line 3 aiapsdfjasdjwekkxz.
Zoom: 4x
END

my $cupt = setup(
	'dpkg_status' => \@installed,
	'packages' => [ compose_package_record('zz', '1') . "Important: yes\n" ],
);

eis($cupt, 'installed', qw(xx yy));
eis($cupt, 'not(installed)', qw(zz));
eis($cupt, 'i', qw(xx yy));

eis($cupt, 'source-package(nn)', qw(xx));
eis($cupt, 'source-package(yy)', qw(yy));
eis($cupt, 'sp(yy)', qw(yy));

eis($cupt, 'source-version(.*8)', qw(xx));
eis($cupt, 'source-version(1)', qw(yy zz));
eis($cupt, 'sv(1)', qw(yy zz));

eis($cupt, 'essential', qw(yy));
eis($cupt, 'e', qw(yy));

eis($cupt, 'important', qw(zz));

eis($cupt, 'description(oqwo)', qw());
eis($cupt, 'description(.*oqwo.*)', qw(yy));
eis($cupt, 'd(.* Line.*)', qw(yy));

eis($cupt, 'provides(mm)', qw(yy));
eis($cupt, 'provides(pp)', qw(yy));
eis($cupt, 'o(pp)', qw(yy));
eis($cupt, 'provides(qq)', qw());

