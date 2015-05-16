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

my %downloads = (
	'tarball' => {
		'name' => "$pv-X.tar.gz",
		'content' => 'X-file',
	},
	'diff' => {
		'name' => "$pv-Y.diff.gz",
		'content' => 'Y-file',
	},
	'dsc' => {
		'name' => "$pv-Z.dsc",
		'content' => 'Z-file',
	}
);

sub compose_source_package {
	my $result = compose_package_record($package, $version);
	$result .= "Checksums-Sha1:\n";
	foreach my $type (keys %downloads) {
		my $record = $downloads{$type};
		my $name = $record->{'name'};
		my $size = length($record->{'content'});
		my $sha1 = sha1_hex($record->{'content'});
		$result .= " $sha1 $size $name\n";
	}
	return $result;
}


sub check_file {
	my $type = shift;
	my $name = $downloads{$type}->{'name'};
	my $expected_content = $downloads{$type}->{'content'};
	is(stdall("cat $name"), $expected_content, "$type is downloaded and its content is right");
}

my $repo_suffix = 'somerepofiles';

sub populate_downloads {
	mkdir $repo_suffix;
	foreach my $value (values %downloads) {
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
	check_file('tarball');
	check_file('diff');
	check_file('dsc');
	like($output, qr/\Q[fakes\/dpkg-source]\E -x $pv-Z.dsc$/, 'dpkg-source call');
}

