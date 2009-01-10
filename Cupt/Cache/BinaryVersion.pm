package Cupt::Cache::BinaryVersion;

import Cupt::Core;
import Cupt::Cache::Relation qw(__parse_relation_line);

# parsing options
our $o_no_parse_relations = 0; # don't parse depends, recommends, conflicts etc
our $o_no_parse_info_onlys = 0; # don't parse maintainer, descriptions, tag, homepage

sub new {
	my ($class, $ref_arg) = @_;
	my $self = {
		avail_as => [],
		# should contain array of hashes
		#	release => {
		#		...
		#	},
		#	ref_base_uri
		#	filename

		priority => undef,
		section => undef,
		installed_size => undef,
		maintainer => undef,
		architecture => undef,
		source_name => undef,
		version => undef,
		essential => undef,
		depends => undef,
		recommends => undef,
		suggests => undef,
		conflicts => undef,
		breaks => undef,
		enhances => undef,
		provides => undef,
		replaces => undef,
		pre_depends => undef,
		size => undef,
		md5sum => undef,
		sha1sum => undef,
		sha256sum => undef,
		short_description => undef,
		long_description => undef,
		homepage => undef,
		task => undef,
		tags => undef,
	};
	# parsing fields
	my ($package_name, $fh, $offset, $ref_base_uri, $ref_release_info) = @$ref_arg;

	$self->{avail_as}->[0]->{ref_base_uri} = $ref_base_uri;
	$self->{avail_as}->[0]->{release} = $ref_release_info;
	$self->{source_name} = $package_name; # default, if not specified

	my $field_name = undef;
	eval {
		my $line;
		# go to starting byte of the entry
		seek $fh, $offset, 0;
		# we have already opened file handle and offset for reading the entry
		while (($line = <$fh>) ne "\n") {
			if (($line =~ m/^ / or $line =~ m/^\t/)) {
				# TODO: remove this bogus '\t' after libobject-declare-perl is fixed
				# part of long description
				$self->{long_description} .= $line unless $o_no_parse_info_onlys;
			} else {
				chomp($line);
				(($field_name, my $field_value) = ($line =~ m/^((?:\w|-)+?): (.*)/)) # '$' implied in regexp
					or mydie("cannot parse line '%s'", $line);

				given ($field_name) {
					# mandatory fields
					when ('Priority') { $self->{priority} = $field_value }
					when ('Section') { $self->{section} = $field_value }
					when ('Installed-Size') { $self->{installed_size} = $field_value }
					when ('Maintainer') { $self->{maintainer} = $field_value unless $o_no_parse_info_onlys }
					when ('Architecture') { $self->{architecture} = $field_value }
					when ('Version') { $self->{version} = $field_value }
					when ('Filename') { $self->{avail_as}->[0]->{filename} = $field_value }
					when ('Size') { $self->{size} = $field_value }
					when ('MD5sum') { $self->{md5sum} = $field_value }
					when ('SHA1') { $self->{sha1sum} = $field_value }
					when ('SHA256') { $self->{sha256sum} = $field_value }
					when ('Description') { $self->{short_description} = $field_value unless $o_no_parse_info_onlys }
					# often fields
					when ('Depends') {
						$self->{depends} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Tag') { $self->{tags} = $field_value unless $o_no_parse_info_onlys }
					when ('Source') { $self->{source_name} = $field_value }
					when ('Homepage') { $self->{homepage} = $field_value unless $o_no_parse_info_onlys }
					when ('Recommends') {
						$self->{recommends} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Suggests') {
						$self->{suggests} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Conflicts') {
						$self->{conflicts} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Replaces') {
						$self->{replaces} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Provides') {
						$self->{provides} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					# rare fields
					when ('Pre-Depends') {
						$self->{pre_depends} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Task') { $self->{homepage} = $field_value }
					when ('Enhances') {
						$self->{enhances} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
					when ('Essential') { $self->{essential} = $field_value }
					when ('Breaks') {
						$self->{breaks} = __parse_relation_line($field_value) unless $o_no_parse_relations;
					}
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
	return bless $self => $class;
}

sub is_hashes_equal {
	my $self = shift;
	my $other = shift;
	return ($self->{md5sum} eq $other->{md5sum} &&
			$self->{sha1sum} eq $other->{sha1sum} &&
			$self->{sha256sum} eq $other->{sha256sum});
}

sub uris {
	my $self = shift;
	map { ${$_->{ref_base_uri}} . '/' . $_->{filename} } @{$self->{avail_as}};
}

1;

