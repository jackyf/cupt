use Test::More tests => 5+3+3+2 + 5*4;

require(get_rinclude_path('common'));

# so compressed file is smaller than uncompressed, as usual
my $other_big_record = compose_package_record('other', 0) . join('', map { "X-Cruft: qwz$_" } 1..100);

my $record = compose_package_record('abcd', 3);

sub get_variant_filter_hook {
	my @compresses = @_;
	return sub {
		my ($variant, undef, undef, $content) = @_;
		(grep { $variant eq $_ } @compresses) ? $content : undef;
	}
}

sub test {
	my ($input, $expected_result, $comment) = @_;
	my ($compresses, $hook_stage, $hook) = @$input;
	my $cupt = setup(
		'releases' => [{
			'packages' => [ $record, $other_big_record ],
			'location' => 'remote',
			'hooks' => {
				'compress' => {
					'input' => get_variant_filter_hook(split(/,/, $compresses)),
					defined($hook) ? ($hook_stage => $hook) : (),
				},
			},
		}]
	);

	subtest $comment => sub {
		check_exit_code("$cupt update", $expected_result, "whether update succeeded");
		check_exit_code("$cupt show abcd", $expected_result, "whether package is fine");
	}
}

sub size_modifier {
	return 'Foobarblah$%*()' . $_[0];
}

sub byte_modifier {
	return substr($_[0], 0, 16) . 'foo' . substr($_[0], 19);
}

sub remover {
	return undef;
}

sub corrupter {
	my ($modifier, $do_variants) = @_;
	return sub {
		my ($variant, undef, undef, $content) = @_;
		return $content unless grep { $variant eq $_ } split(/,/,$do_variants);
		return $modifier->($content);
	}
}

test(['orig'] => 1, 'not compressed');
test(['orig,gz'] => 1, 'orig, gzip');
test(['orig,bz2'] => 1, 'orig, bzip2');
test(['orig,xz'] => 1, 'orig, xz');
test(['orig,gz,bz2,xz'] => 1, 'orig, xz');

test(['gz'] => 1, 'only gz');
test(['bz2'] => 1, 'only bzip2');
test(['xz'] => 1, 'only xz');

test(['bz2,xz'] => 1, 'bzip2, xz');
test(['gz,xz'] => 1, 'gzip, xz');
test([''] => 0, 'no files');

test(['orig', 'write', corrupter(\&remover, 'orig')] => 0, 'even original not available');
test(['orig,gz', 'write', corrupter(\&remover, 'orig')] => 1, 'original not available but gz is');

sub test_corruptions {
	my ($stage, $modifier, $corrupter_comment) = @_;
	test(['gz', $stage, corrupter($modifier, 'gz')] => 0, "only gz (corrupted) - $corrupter_comment");
	test(['gz,xz', $stage, corrupter($modifier, 'gz')] => 1, "gz (corrupted), xz - $corrupter_comment");
	test(['gz,xz', $stage, corrupter($modifier, 'xz')] => 1, "gz, xz (corrupted) - $corrupter_comment");
	test(['gz,xz', $stage, corrupter($modifier, 'gz,xz')] => 0, "all compressed are corrupted - $corrupter_comment");
	test(['orig,gz,xz', $stage, corrupter($modifier, 'gz,xz')] => 1, "all compressed corrupted but original present - $corrupter_comment");
}

test_corruptions('seal', \&size_modifier, 'compressor error');
test_corruptions('write', \&remover, 'not available');
test_corruptions('write', \&size_modifier, 'wrong size');
test_corruptions('write', \&byte_modifier, 'wrong hash sum');

