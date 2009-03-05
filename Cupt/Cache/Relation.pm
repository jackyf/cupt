package Cupt::Cache::Relation;

use 5.10.0;
use strict;
use warnings;

use Exporter qw(import);

use Cupt::Core;

our @EXPORT_OK = qw(&__parse_relation_line &stringify_relation_expressions
		&stringify_relation_expression &parse_relation_expression);

sub new {
	my ($class, $unparsed) = @_;
	my $self = {
		package_name => undef,
		relation_string => undef,
		version_string => undef,
	};
	bless $self => $class;

	if ($unparsed =~ m/^($package_name_regex)/g) {
		# package name is here
		$self->{package_name} = $1;
	} else {
		# no package name, badly
		mydie("failed to parse package name in relation '%s'", $unparsed);
	}

	if ($unparsed =~ m/
			\G # start at end of previous regex
			\s* # possible spaces
			\( # open relation brace
				(
					>=|=|<=|<<|>>|<|> # relation
				)
				\s* # possible spaces
				(
					 $version_string_regex# version
				)
			\) # close relation brace
			$
		/xgc
	)
	{
		# versioned info is here, assigning
		($self->{relation_string}, $self->{version_string}) = ($1, $2);
	} else {
		# no valid versioned info, maybe empty?
		($unparsed =~ m/\G\s*$/g) # empty versioned info, this is also acceptable
			or mydie("failed to parse versioned info in relation '%s'", $unparsed); # what else can we do?..
	}

	return $self;
}

sub stringify {
	my $self = shift;
	my $result = $self->{package_name};
	if (defined($self->{relation_string})) {
		# there is versioned info
		$result .= join('', " (", $self->{relation_string}, ' ', $self->{version_string}, ')');
	}
	return $result;
}

sub stringify_relation_expression ($) {
	my $arg = $_[0];
	if (ref $arg ne 'ARRAY' ) {
		# it's ordinary relation object
		return $arg->stringify();
	} else {
		# it have be an 'OR' group of relations
		return join(" | ", map { $_->stringify() } @$arg);
	}
}

sub stringify_relation_expressions {
	my @relation_strings;
	foreach my $object (@{$_[0]}) {
		push @relation_strings, stringify_relation_expression($object);
	}
	return join(", ", @relation_strings);
}

sub satisfied_by ($$) {
	my ($self, $version_string) = @_;
	if (defined($self->{relation_string})) {
		# relation is defined, checking
		my $comparison_result = Cupt::Core::compare_version_strings($version_string, $self->{version_string});
		given($self->{relation_string}) {
			when('>=') { return ($comparison_result >= 0) }
			when('<') { continue }
			when('<<') { return ($comparison_result < 0) }
			when('<=') { return ($comparison_result <= 0) }
			when('=') { return ($comparison_result == 0) }
			when('>') { continue }
			when('>>') { return ($comparison_result > 0) }
		}
	}
	# no versioned info, so return true
	return 1;
}

=head2

free subroutine, parses relation expression in string form, builds relation expression and returns it

=cut

sub parse_relation_expression ($) {
	my ($relation_expression_string) = @_;

	# looking for OR groups
	my @relations = split / ?\| ?/, $relation_expression_string;
	if (scalar @relations == 1) {
		# ordinary relation
		return new Cupt::Cache::Relation($relations[0]);
	} else {
		# 'OR' group of relations
		return [ map { new Cupt::Cache::Relation($_) } @relations ];
	}
}

sub __parse_relation_line {
	# my $relation_line = $_[0] 
	# or myinternaldie("relation line is not defined");

	my @result;
	while ($_[0] =~ m/(.+?)(?:,\s*|$)/g) {
		push @result, parse_relation_expression($1);
	}
	return \@result;
}

1;

