use Test::More tests => 4+6+2+2;

require(get_rinclude_path('common'));

sub maybe_compose_package {
	my $package = shift;
	return undef unless defined($package);
	return [ compose_package_record($package, 1) ];
}

sub compose_release {
	my ($binary_package, $source_package, $archive) = @_;
	return {
		'packages' => maybe_compose_package($binary_package),
		'sources' => maybe_compose_package($source_package),
		'archive' => $archive,
		'location' => 'remote',
	};
}

sub describe_test_input {
	my $describe_one = sub {
		return '[' . join(" ", map { $_//'-' } @$_) . ']';
	};
	return join(", ", map(&$describe_one, @_));
}

sub check_archive_packages {
	my ($cupt, $type, $package, $archive) = @_;
	return unless defined($package);

	my $command = $type eq 'binary' ? 'show' : 'showsrc';
	my $output = stdall("$cupt $command $package -a --include-archives=$archive");
	my @got_packages = ($output =~ m/^Package: (.*?)$/mg);
	is_deeply(\@got_packages, [$package], "$type package '$package', archive $archive")
			or diag($output);
}

sub test {
	my @inputs = @_;
	my $comment = describe_test_input(@inputs);
	subtest $comment => sub {
		my $cupt = setup(
			'releases' => [ map { compose_release(@$_) } @inputs ]
		);
		check_exit_code("$cupt update", 1, 'update succeeded');
		check_no_partial_files();
		foreach (@inputs) {
			my ($binary_package, $source_package, $archive) = @$_;
			check_archive_packages($cupt, 'binary', $binary_package, $archive);
			check_archive_packages($cupt, 'source', $source_package, $archive);
		}
	}
}


test(['abc', undef, 'xarc']);
test([undef, 'def', 'yarc']);
test(['aa', 'bb', 'xarc']);
test(['aa', 'aa', 'xarc']);

test(['aa', undef, 'xarc'], ['bb', undef, 'yarc']);
test([undef, 'aa', 'xarc'], [undef, 'bb', 'yarc']);
test(['aa', undef, 'xarc'], [undef, 'bb', 'yarc']);
test(['aa', 'ccc', 'xarc'], ['bb', undef, 'yarc']);
test(['aa', 'ccc', 'xarc'], [undef, 'bb', 'yarc']);
test(['aa', 'ccc', 'xarc'], ['bb', 'ddd', 'yarc']);

test(['aa', undef, 'xarc'], ['aa', undef, 'yarc']);
test([undef, 'bb', 'xarc'], [undef, 'bb', 'yarc']);

test(['aa', 'aa-s', 'sid'], [undef, 'bb', 'experimental'], ['cc', undef, 'precise']);
test(
	['aa', undef, 'snap1'],
	['aa', undef, 'snap2'],
	['aa', 'aa-s', 'wheezy'],
	['aa', undef, 'snap3'],
	['aa', undef, 'snap28']
);

