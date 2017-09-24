require(get_rinclude_path('../common'));

sub get_new_extended_states_content {
	open(my $fd, '<', get_extended_states_path()) or
			return '<file not found>';
	my $result = do { local $/; <$fd> };
	return $result;
}

