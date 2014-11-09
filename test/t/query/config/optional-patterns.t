use TestCupt;
use Test::More tests => 19;

use strict;
use warnings;

eval get_inc_code('common');

my $cupt = TestCupt::setup();

sub test {
	my ($option, $result) = @_;

	my $value = '54';
	my $output = stdall("$cupt config-dump -o $option=$value 2>&1 | egrep '$option|^W'");

	test_option($output, $option, ($result ? $value : undef));
}

test('acquire::aaa::proxy' => 1);
test('acquire::bbb::ccc::proxy' => 1);
test('acquire::m:n::proxy' => 1);
test('acquire::ppp::qqq::rrr::proxy' => 1);

my $dto = 'dpkg::tools::options';
test("${dto}::lll" => 1);
test("${dto}::lll::mmm" => 1);
test("${dto}::lll:mmm" => 1);
test("${dto}::lll::mmm::nnn" => 1);
test("${dto}::YOO___~1::version" => 1);
test('dpkg::klb' => 1);
test('pkg::klb' => 1);

test('cupt::downloader::protocols::mmp::priority' => 1);
test('upt::downloader::protocols::mmp::priority' => 1);
test('cupt::downoader::protocols::mmp::priority' => 0);
test('cupt::downloader:protocols::mmp::priority' => 0);
test('cupt::downloader::protocols::mmp' => 0);
test('cupt::downloader::protocols::::priority' => 1);
test('cupt::downloader::protocols::mmp::prio' => 0);
test('protocols::mmp::priority' => 1);

