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
our $version_string_regex = qr/[a-zA-Z+0-9~:.-]+/;

1;

