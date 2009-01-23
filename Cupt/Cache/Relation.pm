package Cupt::Cache::Relation;

use 5.10.0;
use strict;
use warnings;

use Exporter qw(import);

use Cupt::Core;

our @EXPORT_OK = qw(&__parse_relation_line &stringify_relations &stringify_relation_or_group);

sub new {
	my ($class, $unparsed) = @_;
	my $self = {
		package_name => undef,
		relation => undef,
		version => undef,
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
		($self->{relation}, $self->{version}) = ($1, $2);
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
	if (defined($self->{relation})) {
		# there is versioned info
		$result .= join('', " (", $self->{relation}, ' ', $self->{version}, ')');
	}
	return $result;
}

sub stringify_relation_or_group ($) {
	my $arg = $_[0];
	if (UNIVERSAL::isa($arg, 'Cupt::Cache::Relation')) {
		# it's ordinary relation object
		return $arg->stringify();
	} else {
		# it have be an 'OR' group of relations
		return join(" | ", map { $_->stringify() } @$arg);
	}
}

sub stringify_relations {
	my @relation_strings;
	foreach my $object (@{$_[0]}) {
		push @relation_strings, stringify_relation_or_group($object);
	}
	return join(", ", @relation_strings);
}

sub satisfied_by ($$) {
	my ($self, $version_string) = @_;
	if (defined($self->{relation})) {
		# relation is defined, checking
		my $comparison_result = return Cupt::Core::compare_version_strings($version_string, $self->{version});
		given($self->{relation}) {
			when('<') { continue }
			when('<<') { return ($comparison_result < 0) }
			when('<=') { return ($comparison_result <= 0) }
			when('=') { return ($comparison_result == 0) }
			when('>=') { return ($comparison_result >= 0) }
			when('>') { continue }
			when('>>') { return ($comparison_result > 0) }
		}
	}
	# no versioned info, so return true
	return 1;
}

sub __parse_relation_line {
	# my $relation_line = $_[0] 
	# or myinternaldie("relation line is not defined");

	my @result;
	while ($_[0] =~ m/(.+?)(?:,\s*|$)/g) {
		# looking for OR groups
		my @relations = split / ?\| ?/, $1;
		if (scalar @relations == 1) {
			# ordinary relation
			push @result, new Cupt::Cache::Relation($relations[0]);
		} else {
			# 'OR' group of relations
			push @result, [ map { new Cupt::Cache::Relation($_) } @relations ];
		}
	}
	return \@result;
}

1;

