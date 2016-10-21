use Test::More tests => 2+5+3+2+11;

require(get_rinclude_path('../common'));

my $desc_translation_hash = '111ccc';

sub our_package {
	my ($version, $free_field_value) = @_;
	my $record = compose_package_record('pabc', $version);
	$record .= "Description-md5: $desc_translation_hash\n";
	my @other_records = map { compose_package_record("other$_", 0) } 1..143;
	return [ $record, @other_records ];
}

sub our_translation {
	my ($desc) = @_;
	my $record = compose_translation_record(undef, 'en', $desc_translation_hash, $desc);
	my @other_records = map { compose_translation_record("other$_", 'fr', 'abc', "def$_") } 1..234;
	return [ $record, @other_records ];
}

sub match_variant {
	my ($variants, $variant, $content) = @_;
	if (grep { $variant eq $_ } split(',', $variants)) {
		return $content;
	} else {
		return undef;
	}
}

sub get_input_hook {
	my $variants = shift;
	return sub {
		my ($variant, $kind, undef, $content) = @_;
		if ($variants eq 'orig(pkg),diff') {
			return $content if ($variant eq 'orig' and $kind =~ m/Packages/);
		}
		return match_variant($variants, $variant, $content);
	}
}

sub get_corruption_hook_kv {
	my $input = shift;
	return () unless defined $input;
	my ($difffile, $hookname, $override_content) = @$input;
	return ($hookname => sub {
		my (undef, $kind, undef, $content) = @_;
		return $content unless $kind =~ m!/$difffile!;
		if (ref($override_content) eq 'CODE') {
			return $override_content->($content);
		} else {
			return $override_content;
		}
	});
}

sub byte_changer {
	$_[0]=~s/./%/;
	return $_[0];
}

sub test {
	my ($input, $exp_version, $exp_description, $comment) = @_;

	my $cupt = setup(
		'releases' => [{
			'packages' => our_package('0a'),
			'translations' => {
				'en' => our_translation('startdesc'),
			},
			'location' => 'local'
		}]
	);

	update_remote_releases({
		'location' => 'remote',
		'hooks' => {
			'diff' => {
				'input' => get_input_hook($input->{var}//'orig'),
				(get_corruption_hook_kv($input->{hook}))
			},
			'compress' => {
				'input' => get_input_hook('orig,gz'),
				(get_corruption_hook_kv($input->{zhook}))
			},
		},
		'previous' => {
			'packages' => $input->{pkg},
			'translations' => {
				'en' => $input->{tr},
			}
		},
		'packages' => our_package('2y'),
		'translations' => {
			'en' => our_translation('enddesc'),
		}
	});

	subtest $comment => sub {
		my () = @_;
		system("$cupt update");
		check_no_partial_files();
		my $output = stdall("$cupt show pabc");
		like($output, qr/^Version: \Q$exp_version\E$/m, 'version is right');
		like($output, qr/^Description: \Q$exp_description\E$/m, 'description is right');
	}
}

*pp = \&our_package;
*tt = \&our_translation;

test({var=>''} => qw(0a startdesc), 'no updates');
test({var=>'orig'} => qw(2y enddesc), 'regular update');

test({var=>'diff', pkg=>{5=>pp('1x')}} =>
		qw(0a startdesc), 'one diff, no history match');
test({var=>'diff', pkg=>{2=>pp('0a'),5=>pp('1x')}} =>
		qw(1x startdesc), 'two diffs, good match');
test({var=>'diff', pkg=>{1=>pp('0.1'),3=>pp('0a'),5=>pp('1x')}} =>
		qw(1x startdesc), 'three diffs, good match from second');
test({var=>'diff', pkg=>{2=>pp('0.2'), 4=>pp('0.4'), 6=>pp('0a'), 8=>pp('1x'), 9=>pp('1z')}} =>
		qw(1z startdesc), 'five diffs, match from third');
test({var=>'diff', pkg=>{2=>pp('0a'), 4=>pp('0.4'), 6=>pp('0.10'), 8=>pp('1x'), 9=>pp('1z')}} =>
		qw(1z startdesc), 'five diffs, match from third');

test({var=>'diff', tr=>{3=>tt('bgdesc'), 7=>tt('zdesc')}} =>
		qw(0a startdesc), 'no translation diff match');
test({var=>'orig(pkg),diff', tr=>{3=>tt('startdesc'), 7=>tt('zdesc')}} =>
		qw(2y zdesc), 'translation diff match');
test({var=>'orig(pkg),diff', tr=>{3=>tt('startdesc'), 7=>tt('zdesc'), 8=>tt('zzdesc'), 9=>tt('qdesc')}} =>
		qw(2y qdesc), 'several translation diffs');

test({var=>'orig,diff', pkg=>{5=>pp('1x')}} =>
		qw(2y enddesc), 'no history match, should use full index');
test({var=>'orig,diff', pkg=>{4=>pp('0a'),5=>pp('1x')}} =>
		qw(1x enddesc), 'diff match, should not use full index');

my @new = qw(22 startdesc);
my @old = qw(0a startdesc);
my @matching_params = ( var=>'diff', pkg=>{6=>pp('0a'), 8=>pp('11'), 9=>pp('22')} );
test({@matching_params} => @new, 'hooks: no');
test({@matching_params, hook=>[8,'write',undef]} => @old, 'hooks: first diff missing');
test({@matching_params, hook=>[9,'write',undef]} => @old, 'hooks: last diff missing');
test({@matching_params, hook=>['Index','write',undef]} => @old, 'hooks: diff index missing');
test({@matching_params, hook=>['Index','write',"Tuias: iaps\n"]} => @old, 'hooks: diff index size does not match');
test({@matching_params, hook=>['Index','write',\&byte_changer]} => @old, 'hooks: diff index hash sums do not match');
test({@matching_params, hook=>['Index','seal',"Tuias: iaps\n"]} => @old, 'hooks: garbage in a diff index');
test({@matching_params, hook=>[8,'write',')&)791723']} => @old, 'hooks: compressed diff size does not match');
test({@matching_params, hook=>[8,'write',\&byte_changer]} => @old, 'hooks: compressed diff hash sums do not match');
test({@matching_params, zhook=>[8,'seal','uqojds']} => @old, 'hooks: garbage in compressed diff');
test({@matching_params, hook=>[8,'seal','701jjasds70']} => @old, 'hooks: garbage in a diff');

