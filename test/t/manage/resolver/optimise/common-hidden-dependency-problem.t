use TestCupt;
use Test::More tests => 11;

use strict;
use warnings;

sub compose_branch {
	my ($number, $is_broken, $is_last) = @_;

	my $get_dependee = sub {
		my ($package) = @_;
		return $package . ($is_last ? $number : ($number+1));
	};

	my $dependee = $get_dependee->('ttt') . ', ' . $get_dependee->('vvv');
	if ($is_broken) {
		$dependee =~ s/,/, uuu-broken,/; # ttt < uuu < vvv
	}
	my $aux_relation = $is_last ? "Conflicts: imp\n" : '';
	
	my $relations = "Depends: $dependee\n";

	my $result = '';
	foreach my $package (qw(ttt vvv)) {
		foreach my $letter (qw(a b c)) {
			$result .= entail(compose_package_record("$package$number", "$number.$letter") . $relations);
		}
	}

	return $result;
}

my $branches_comment;

sub compose_branches {
	my ($total) = @_;

	my $result = '';

	my $broken_level = int($total/2);
	foreach my $level (1..$total) {
		my $is_broken = $level == $broken_level;
		my $is_last = $level == $total;
		$result .= compose_branch($level, $is_broken, $is_last);
	}

	$branches_comment = "tree level: $total, broken level: $broken_level";

	return $result;
}

my $cupt;

sub lsetup {
	$cupt = TestCupt::setup(
		'dpkg_status' =>
			entail(compose_installed_record('imp', 0)),
		'packages' =>
			entail(compose_package_record('ttt1', '1.z') . "Breaks: imp\n") .
			compose_branches(@_),
		'preferences' =>
			compose_version_pin_record('*', '*a', 400) .
			compose_version_pin_record('*', '*b', 300) .
			compose_version_pin_record('*', '*c', 200),
	);
}

sub test {
	lsetup(@_);

	my $offer = get_first_offer("$cupt satisfy 'ttt1 | vvv1'");
	is(get_offered_version($offer, 'imp'), get_empty_version(), "package 'imp' gets removed ($branches_comment)") or
			diag($offer);
}

foreach (2..12) {
	test($_);
}

