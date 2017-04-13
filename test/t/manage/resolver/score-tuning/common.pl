sub generate_n_installed_packages {
	return join('', map { entail(compose_installed_record("p$_", 1) . "Provides: p\n") } (1..$_[0]));
}

