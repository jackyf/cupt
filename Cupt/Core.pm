#***************************************************************************
#*   Copyright (C) 2008-2009 by Eugene V. Lyubimkin                        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU General Public License                  *
#*   (version 3 or above) as published by the Free Software Foundation.    *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU General Public License for more details.                          *
#*                                                                         *
#*   You should have received a copy of the GNU GPL                        *
#*   along with this program; if not, write to the                         *
#*   Free Software Foundation, Inc.,                                       *
#*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the Artistic License, which comes with Perl     *
#***************************************************************************
package Cupt::Core;

=head1 NAME

Cupt::Core - core subroutines for Cupt

=cut

INIT { require Carp; $SIG{__WARN__} = \&Carp::confess; $SIG{__DIE__} = \&Carp::confess; }

use 5.10.0;
use warnings;
use strict;

use Exporter qw(import);
our @EXPORT = qw(
	&mywarn &myerr &myredie &mydie &myinternaldie &mycatch &mydebug
	$package_name_regex $version_string_regex &human_readable_size_string &__
	&is_version_string_native &are_hash_sums_present &compare_hash_sums
	&glob_to_regex);

# configuring the translator
eval {
	require Locale::gettext;
};
if ($@) {
	# require failed, most probably, we don't have Locale::gettext installed
	*__ = sub { $_[0] };
} else {
	# require succeeded
	Locale::gettext::textdomain("cupt");
	do {
		no warnings;
		*__ = *Locale::gettext::gettext;
	};
}

sub _myprinterror {
	say STDERR shift;
}

sub _myformat {
	return sprintf(__(shift), @_);
}

sub mywarn {
	_myprinterror("W: " . _myformat(@_));
}

sub myerr {
	_myprinterror("E: " . _myformat(@_));
}

sub myredie() {
	die 'Cupt::Error';
}

sub mydie {
	myerr @_;
	myredie();
}

sub myinternaldie {
	_myprinterror("E: " . __("internal error: ") . _myformat(@_));
	die;
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

sub mydebug {
	print STDERR "D: ";
	say STDERR sprintf(shift, @_);
}

=head1 REGEXES

=head2 package_name_regex

regular expression to check package name string

=cut

our $package_name_regex = qr/[a-z_0-9.+-]+/;

=head2 version_string_regex

regular expression to check version string

=cut

our $version_string_regex =
	qr/ (?<EPOCH> # name of this match is 'EPOCH'
			\d+: # epoch
		)? # which is non-mandatory
		(
			# should start with a number really
			(?:
				(?(<EPOCH>):|0) # allow ':' if there was valid epoch, otherwise something neutral
					| # or
				[a-zA-Z+0-9~.-] # upstream version allowed characters
			)+? # '?' to not eat last '-' before debian revision
		)
		(?:
			-
			([a-zA-Z+0-9~_.]+) # debian revision
		)? # which is non-mandatory
	/x;

my $__version_symbol_sort_string = "~ _abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+-.:";

sub __compare_letter_symbol ($$) {
	my ($left, $right) = @_;
	return index($__version_symbol_sort_string, $left) <=> index($__version_symbol_sort_string, $right);
}

sub __compare_version_part ($$) {
	my ($left, $right) = @_;

	# take into account that preceding zeroes in numbers must be stripped
	foreach ($left, $right) {
		# strip out any group of zeroes, which have non-zero after and non-number or nothing before
		# but don't strip zero in the start of string
		s/(?:\D|^)\K 0+ (?=\d)//xg;
	}

	# add "empty" characters to make strings char-comparable
	# 1 will be less than all but '~' character
	if (length($left) > length($right)) {
		$right .= ' ' x (length($left) - length($right));
	} elsif (length($left) < length($right)) {
		$left .= ' ' x (length($right) - length($left));
	}

	my $len = length($left);
	my $last_char_idx = $len - 1;
	my $current_part_is_digit = 0;
	foreach my $idx (0 .. $last_char_idx) {
		my $left_char = substr($left, $idx, 1);
		my $right_char = substr($right, $idx, 1);

		if ($left_char ne $right_char) {
			# no draw here, one will be the winner

			my $left_char_is_digit = $left_char =~ m/\d/;
			my $right_char_is_digit = $right_char =~ m/\d/;

			if ($left_char_is_digit && $right_char_is_digit) {
				# then we have to check lengthes of futher numeric parts
				# if some numeric part have greater length, then it will be
				# the winner, otherwise previous comparison result is used
				# examples: 'abc120de' < 'abc123', but 'abc1200' > 'abc123'
				my $left_num_pos = $idx;
				while ($left_num_pos+1 < $len and
						substr($left, $left_num_pos+1, 1) =~ m/\d/)
				{
					++$left_num_pos;
				}

				my $right_num_pos = $idx;
				while ($right_num_pos+1 < $len and
						substr($right, $right_num_pos+1, 1) =~ m/\d/)
				{
					++$right_num_pos;
				}

				my $num_pos_comparison_result = ($left_num_pos <=> $right_num_pos);
				return $num_pos_comparison_result unless $num_pos_comparison_result == 0;

				# the same number of the digits
				return $left_char <=> $right_char;
			}

			if (!$left_char_is_digit && !$right_char_is_digit) {
				return __compare_letter_symbol($left_char, $right_char);
			} elsif ($left_char eq ' ' || $right_char eq ' ') {
				return $left_char eq ' ' ? -1 : 1;
			} elsif ($left_char_is_digit) {
				return $current_part_is_digit ? 1 : -1;
			} else { # right char is digit
				return $current_part_is_digit ? -1 : 1;
			}
		} else {
			$current_part_is_digit = ($left_char =~ m/\d/);
		}
	}
	# oh, we are out of strings here... well, they are equal then
	return 0;
};

=head1 SUBROUTINES

=head2 compare_version_strings

free subroutine, compares two version strings

Parameters:

I<first_version_string>

I<second_version_string>

Returns:

=over

=item *

-1, if the I<first_version_string> is less than I<second_version_string>

=item *

0, if the I<first_version_string> is equal to I<second_version_string>

=item *

1, if the I<first_version-string> if greater than I<second_version_string>

=back

=cut

sub compare_version_strings($$) {
	# version part can be epoch, version and debian revision

	my ($left, $right) = @_;
	my ($left_epoch, $left_upstream, $left_revision) = ($left =~ /^$version_string_regex$/);
	my ($right_epoch, $right_upstream, $right_revision) = ($right =~ /^$version_string_regex$/);

	if (not defined $left_epoch) {
		$left_epoch = '0';
	} else {
		chop($left_epoch);
	}

	if (not defined $right_epoch) {
		$right_epoch = '0';
	} else {
		chop($right_epoch);
	}

	my $epoch_comparison_result = $left_epoch <=> $right_epoch;
	return $epoch_comparison_result unless $epoch_comparison_result == 0;

	my $upstream_comparison_result = __compare_version_part($left_upstream, $right_upstream);
	return $upstream_comparison_result unless $upstream_comparison_result == 0;

	$left_revision //= '0';
	# first numeric part of revision may be empty, so adding it, see Policy §5.6.12
	$left_revision = 'a' . $left_revision;

	$right_revision //= '0';
	# same for right part
	$right_revision = 'a' . $right_revision;

	my $revision_comparison_result = __compare_version_part($left_revision, $right_revision);
	return $revision_comparison_result;
}

sub human_readable_size_string ($) {
	my ($bytes) = @_;

	return sprintf("%.0fB", $bytes) if ($bytes < 10*1024);
	return sprintf("%.1fKiB", ($bytes / 1024)) if ($bytes < 100*1024);
	return sprintf("%.0fKiB", ($bytes / 1024)) if ($bytes < 10*1024*1024);
	return sprintf("%.1fMiB", ($bytes / (1024*1024))) if ($bytes < 100*1024*1024);
	return sprintf("%.0fMiB", ($bytes / (1024*1024))) if ($bytes < 10*1024*1024*1024);
	return sprintf("%.1fGiB", ($bytes / (1024*1024*1024)));
}

=head2 is_version_string_native

returns boolean answer, true if the version is version for Debian native
package, and false otherwise

=cut

sub is_version_string_native ($) {
	# does it contain minus sign?
	return ($_[0] !~ m/-/);
}

=head2 are_hash_sums_present

returns boolean answer, true if at least one valid hash sum (md5, sha1, sha256)
present

=cut

sub are_hash_sums_present ($) {
	my ($ref_hash) = @_;
	return (defined $ref_hash->{'md5sum'} ||
		defined $ref_hash->{'sha1sum'} ||
		defined $ref_hash->{'sha256sum'});
}

=head2 compare_hash_sums

returns boolean answer, whether hash sums in the specified hashes match

Parameters:

I<left> - hash

I<right> - hash

=cut

sub compare_hash_sums ($$) {
	my ($left, $right) = @_;
	my $sums_count = 0;
	foreach my $hash_sum_name (qw(md5sum sha1sum sha256sum)) {
		if (defined $left->{$hash_sum_name} and defined $right->{$hash_sum_name}) {
			++$sums_count;
			return 0 if $left->{$hash_sum_name} ne $right->{$hash_sum_name};
		}
	}
	return $sums_count;
}

=head2 glob_to_regex

modifies the string parameter in-place

=cut

sub glob_to_regex ($) {
	$_[0] = quotemeta($_[0]);
	$_[0] =~ s/\\\?/\./g;
	$_[0] =~ s/\\\*/.*?/g;
}

1;

