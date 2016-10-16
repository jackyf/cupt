use Test::More tests => 5+3+3 + 5*4;

require(get_rinclude_path('common'));

# so compressed file is smaller than uncompressed, as usual
my $other_big_record = compose_package_record('other', 0) . join('', map { "X-Cruft: qwz$_" } 1..100);

my $record = compose_package_record('abcd', 3);

sub test {
	my ($compresses, $hook, $expected_result, $comment) = @_;
	my $cupt = setup(
		'releases' => [{
			'packages' => [ $record, $other_big_record ],
			'location' => 'remote',
			'variants' => {
				'compress' => [split(/,/, $compresses)],
			},
			'hooks' => {
				'file' => $hook,
			}
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
	my ($do_stage, $modifier, $do_compressors) = @_;
	return sub {
		my ($stage, $kind, $entry, $content) = @_;
		my ($compressor) = ($kind =~ m/.*\.(.*)$/);
		return $content unless $stage eq $do_stage;
		return $content unless defined $compressor;
		return $content unless grep { $compressor eq $_ } split(/,/,$do_compressors);
		return $modifier->($content);
	}
}

test('orig', undef, 1, 'not compressed');
test('orig,gz', undef, 1, 'orig, gzip');
test('orig,bz2', undef, 1, 'orig, bzip2');
test('orig,xz', undef, 1, 'orig, xz');
test('orig,gz,bz2,xz', undef, 1, 'orig, xz');

test('gz', undef, 1, 'only gz');
test('bz2', undef, 1, 'only bzip2');
test('xz', undef, 1, 'only xz');

test('bz2,xz', undef, 1, 'bzip2, xz');
test('gz,xz', undef, 1, 'gzip, xz');
test('', undef, 0, 'no files');

sub test_corruptions {
	my ($stage, $modifier, $corrupter_comment) = @_;
	test('gz', corrupter($stage, $modifier, 'gz'), 0, "only gz (corrupted) - $corrupter_comment");
	test('gz,xz', corrupter($stage, $modifier, 'gz'), 1, "gz (corrupted), xz - $corrupter_comment");
	test('gz,xz', corrupter($stage, $modifier, 'xz'), 1, "gz, xz (corrupted) - $corrupter_comment");
	test('gz,xz', corrupter($stage, $modifier, 'gz,xz'), 0, "all compressed are corrupted - $corrupter_comment");
	test('orig,gz,xz', corrupter($stage, $modifier, 'gz,xz'), 1, "all compressed corrupted but original present - $corrupter_comment");
}

test_corruptions('pre', \&size_modifier, 'compressor error');
test_corruptions('post', \&remover, 'not available');
test_corruptions('post', \&size_modifier, 'wrong size');
test_corruptions('post', \&byte_modifier, 'wrong hash sum');

