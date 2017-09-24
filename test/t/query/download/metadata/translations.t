use Test::More tests => 2;

require(get_rinclude_path('common'));

sub get_translation_record_for_lang {
	my $lang = shift;
	return compose_translation_record('xyz', $lang, '789def', 'whatever')
}

sub fetch_pair_if_translation {
	my $tr_path_prefix = '_aaa_ccc_i18n_Translation';

	return () if m/index0/;
	my ($lang) = m/$tr_path_prefix-(.*)/;
	return () unless defined($lang);

	return ($lang => $_);
}

sub test {
	my ($order, $expected_translations) = @_;
	my $cupt = setup(
		'releases' => [{
			'location' => 'remote',
			'archive' => 'aaa',
			'component' => 'ccc',
			'packages' => [],
			'translations' => {
				'en' => [ get_translation_record_for_lang('en') ],
				'fi' => [ get_translation_record_for_lang('fi') ],
				'de' => [ get_translation_record_for_lang('de') ],
				'pt' => [ get_translation_record_for_lang('pt') ],
				'pt_BR' => [ get_translation_record_for_lang('pt_BR') ],
			}
		}]
	);

	my $dir = 'var/lib/cupt/lists';

	subtest "order: $order" => sub {
		check_exit_code("$cupt update -o cupt::languages::indexes=$order", 1, 'metadata update succeeded');
		check_no_partial_files();

		my @got_files = glob("$dir/*");
		my %got_translations = map(&fetch_pair_if_translation, @got_files);
		my @got_langs = sort keys %got_translations;
		is_deeply(\@got_langs, $expected_translations, 'downloaded right translation files');
		while (my ($lang, $path) = each %got_translations) {
			is(`cat $path`, get_translation_record_for_lang($lang)."\n", "content is right for translation '$lang'");
		}
	}
}

$ENV{LC_ALL} = "fi_FI.UTF-8";
test("en,environment", [qw(en fi)]);
test("pt_BR,fi", [qw(fi pt pt_BR)]);

