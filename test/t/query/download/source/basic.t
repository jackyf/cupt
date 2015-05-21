use TestCupt;
use Test::More tests => 1;
use Digest::SHA qw(sha1_hex);
use Cwd;
use IPC::Run3;

use strict;
use warnings;

my $package = 'pkg12';
my $version = '5.4-3';
my $pv = "${package}_${version}";

my @downloads = (
	{
		'name' => "$pv-X.tar.gz",
		'content' => 'X-file',
	},
	{
		'name' => "$pv-Y.diff.gz",
		'content' => 'Y-file',
	},
	{
		'name' => "$pv-Z.dsc",
		'content' => 'Z-file',
	}
);

sub compose_source_package {
	my $result = compose_package_record($package, $version);
	$result .= "Checksums-Sha1:\n";
	foreach my $record (@downloads) {
		my $name = $record->{'name'};
		my $size = length($record->{'content'});
		my $sha1 = sha1_hex($record->{'content'});
		$result .= " $sha1 $size $name\n";
	}
	return $result;
}


sub check_file {
	my $record = shift;
	my $name = $record->{'name'};
	my $expected_content = $record->{'content'};
	is(stdall("cat $name"), $expected_content, "$name is downloaded and its content is right");
}

my $repo_suffix = 'somerepofiles';

sub populate_downloads {
	mkdir $repo_suffix;
	foreach my $value (@downloads) {
		my $name = "$repo_suffix/" . $value->{'name'};
		my $content = $value->{'content'};
		run3("cat", \$content, $name, \undef);
	}
}

sub prepare {
	setup();

	my $cupt = setup(
		'sources2' => [
			{
				'scheme' => 'copy',
				'hostname' => cwd() . "/$repo_suffix",
				'content' => entail(compose_source_package()),
			},
		],
	);

	populate_downloads();

	return $cupt;
}

subtest 'default' => sub {
	my $cupt = prepare();
	my $output = stdall("$cupt source $package");
	foreach (@downloads) {
		check_file($_);
	}
	like($output, qr/\Q[fakes\/dpkg-source]\E -x $pv-Z.dsc$/, 'dpkg-source call');
}

