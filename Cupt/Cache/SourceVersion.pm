package Cupt::Cache::SourceVersion;
# TODO: full rework

import Cupt::Core;
import Cupt::Cache::Relation;

sub new {
	my $class = shift;
	my $self = fields::new($class);

	# parsing fields
	my $ref_lines = shift;

	my $field_name = undef;
	eval {
		foreach my $line (@$ref_lines) {
			if ($line =~ m/^ /) {
				# part of long description
				$self->{long_description} .= $line;
			} else {
				(($field_name, my $field_value) = ($line =~ m/^((\w|-)+?): (.*)$/))
					or mydie("cannot parse line '%s'", $line);

				given ($field_name) {
					when ('Priority') { $self->{priority} = $field_value }
					when ('Section') { $self->{section} = $field_value }
					when ('Installed-Size') { $self->{installed_size} = $field_value }
					when ('Maintainer') { $self->{maintainer} = $field_value }
					when ('Source') { $self->{source_name} = $field_value }
					when ('Architecture') { $self->{architecture} = $field_value }
					when ('Version') { $self->{version} = $field_value }
					when ('Depends') { $self->{depends} = __parse_relation_line($line) }
					when ('Recommends') { $self->{recommends} = __parse_relation_line($line) }
					when ('Suggests') { $self->{suggests} = __parse_relation_line($line) }
					when ('Pre-Depends') { $self->{pre_depends} = __parse_relation_line($line) }
					when ('Enhances') { $self->{enhances} = __parse_relation_line($line) }
					when ('Breaks') { $self->{breaks} = __parse_relation_line($line) }
					when ('Provides') { $self->{provides} = __parse_relation_line($line) }
					when ('Conflicts') { $self->{conflicts} = __parse_relation_line($line) }
					when ('Filename') { push @{$self->{uris}}, $field_value }
					when ('Size') { $self->{size} = $field_value }
					when ('MD5sum') { $self->{md5sum} = $field_value }
					when ('SHA1') { $self->{sha1sum} = $field_value }
					when ('SHA256') { $self->{sha256sum} = $field_value }
					when ('Description') { $self->{short_description} = $field_value }
				}
				undef $field_name;
			}
		}
	};
	if (mycatch()) {
		if (defined($field_name)) {
			myerr("error while parsing field '%s'", $field_name);
		}
		myredie();
	}

	return $self;
}

sub is_hashes_equal {
	my $self = shift;
	my $other = shift;
	return ($self->{md5sum} eq $other->{md5sum} &&
			$self->{sha1sum} eq $other->{sha1sum} &&
			$self->{sha256sum} eq $other->{sha256sum});
}

1;


