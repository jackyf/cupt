package Cupt::Core;

use 5.10.0;
use warnings;
use strict;

require Exporter;
our @ISA = ("Exporter");
our @EXPORT = qw(
	&myprint &mywarn &myerr &myredie &mydie &myinternaldie &mycatch
	$package_name_regex $version_string_regex);

use Locale::gettext;

#sub __ {
#	return gettext(shift);
#}

textdomain("cupt");

sub myprint {
	print sprintf(gettext(shift), @_);
}

sub mywarn {
	print "W: ";
	myprint @_;
	print "\n";
}

sub myerr {
	print "E: ";
	myprint @_;
	print "\n";
}

sub myredie() {
	die 'Cupt::Error';
}

sub mydie {
	myerr @_;
	myredie();
}

sub myinternaldie {
	print "E: ", gettext("internal error: ");
	myprint @_;
	print "\n";
	exit 255;
}

sub mycatch() {
	# This subroutine is inspired from David Golden's Exception::Class::TryCatch::&catch
    my $err = $@;
    if ($err =~ m/^Cupt::Error/) {
        return 1;
	} elsif ($err ne "") {
		# some other error received, propagate it
		die $err;
    } else {
        return 0;
    }
}

our $package_name_regex = qr/[a-z_0-9.+-]+/;

our $version_string_regex =
	qr/ (?<EPOCH> # name of this match is 'EPOCH'
			\d+: # epoch
		)? # which is non-mandatory
		(
			(?:
				(?(<EPOCH>):|0) # allow ':' if there was valid epoch, otherwise something neutral
					| # or
				[a-zA-Z+0-9~.-]+? # upstream version allowed characters
			)+? # '?' to not eat last '-' before debian revision
		)
		(
			-
			[a-zA-Z+0-9~.]+ # debian revision
		)? # which is non-mandatory
	/x;

sub compare_version_strings($$) {
	# version part can be epoch, version and debian revision
	my $compare_version_part = sub {
		my $compare_symbol = sub {
			my ($left, $right) = @_;
			my $left_ord = ord($left);
			my $right_ord = ord($right);
			$left_ord = 0 if $left eq '~';
			$right_ord = 0 if $right eq '~';
			return $left_ord <=> $right_ord;
		};

		my ($left, $right) = @_;

		#if (length($left) > 
	};
	# TODO: implement comparing versions

	my ($left_epoch, $left_version, $left_revision) = ~/(\d+:)?$version_string_regex(-\d+)?/;
	# the above regular expression cannot return false because in worst case
	# whole version number was been already checked for $version_string_regex
	return $_[0] cmp $_[1];
}

1;

