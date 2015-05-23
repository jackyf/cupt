use Digest::SHA qw(sha1_hex);
use Cwd;
use IPC::Run3;

use strict;
use warnings;

my $repo_suffix = 'somerepofiles';

sub get_default_source_package {
	my $package = 'pkg12';
	my $version = '5.4-3';
	my $pv = "${package}_${version}";

	return {
		'package' => $package,
		'version' => $version,
		'files' => [
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
		],
	};
}

sub compose_source_record {
	my ($package, $version, %params) = @_;

	my $result = compose_package_record($package, $version);
	$result .= "Checksums-Sha1:\n";
	foreach my $record (@{$params{'files'}}) {
		my $name = $record->{'name'};
		my $size = length($record->{'content'});
		my $sha1 = sha1_hex($record->{'content'});
		$result .= " $sha1 $size $name\n";
	}
	return $result;
}

sub populate_downloads {
	my $files = shift;
	mkdir $repo_suffix;
	foreach my $value (@$files) {
		my $name = "$repo_suffix/" . $value->{'name'};
		my $content = $value->{'content'};
		run3("cat", \$content, $name, \undef);
	}
}

sub prepare {
	my $sp = shift;

	my $cupt = setup(
		'sources2' => [
			{
				'scheme' => 'copy',
				'hostname' => cwd() . "/$repo_suffix",
				'content' => entail(compose_source_record($sp->{'package'}, $sp->{'version'}, %$sp)),
			},
		],
	);

	populate_downloads($sp->{'files'});

	return $cupt;
}

sub check_file {
	my $record = shift;
	my $name = $record->{'name'};
	my $expected_content = $record->{'content'};
	is(stdall("cat $name"), $expected_content, "$name is downloaded and its content is right");
}

