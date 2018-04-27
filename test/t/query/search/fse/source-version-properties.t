use Test::More tests => 5;

require(get_rinclude_path('FSE'));


my $upl = 'Uploaders: Name Surname <ghf@valley.mars>, justemail@server.domain';

my $cupt = setup(
	'sources' => [ compose_package_record('abc', 0) . "$upl\n" ],
);

eis_source($cupt, 'uploaders(.*w.*)' => qw());
eis_source($cupt, 'uploaders(.*v.*)' => qw(abc));
eis_source($cupt, 'u(.*justemail.*)' => qw(abc));
eis_source($cupt, 'u(.*Surname.*)' => qw(abc));
eis_source($cupt, 'u(/,/)' => qw());

