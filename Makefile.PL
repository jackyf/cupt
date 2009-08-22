use ExtUtils::MakeMaker;

require 5.005;

WriteMakefile(
	NAME => 'Cupt',
	VERSION => '0.0.1',
	PREREQ_PM => {
		'Graph'	=> 0,
		'WWW::Curl' => 4.05,
		'URI' => 0,
		'File::Temp' => 0,
		'File::Copy' => 0,
		'Term::Size' => 0,
		'Term::ReadLine' => 0,
		'Parse::RecDescent' => 0,
		'Digest::MD5' => 0,
		'Digest::SHA' => 0,
		'Getopt::Long' => 0,
		'Test::More' => 0,
		'Time::HiRes' => 0,
		'List::MoreUtils' => 0,
	},
);

