from python.lib.testcase import KphpServerAutoTestCase


class TestRequestLimits(KphpServerAutoTestCase):
    def test_query_limit(self):
        resp = self.kphp_server.http_get(uri="/{}".format("a" * 15000))
        self.assertEqual(resp.status_code, 418)

        resp = self.kphp_server.http_get(uri="/a?{}".format("a" * 15000))
        self.assertEqual(resp.status_code, 200)

        resp = self.kphp_server.http_get(uri="/{}".format("a" * 17000))
        self.assertEqual(resp.status_code, 414)

        resp = self.kphp_server.http_get(uri="/a?{}".format("a" * 17000))
        self.assertEqual(resp.status_code, 414)

    def test_headers_limit(self):
        resp = self.kphp_server.http_get(
            headers={"x" * 4000: "y"})
        self.assertEqual(resp.status_code, 200)

        resp = self.kphp_server.http_get(
            headers={"x" * 5000: "y"})
        self.assertEqual(resp.status_code, 400)

        resp = self.kphp_server.http_get(
            headers={"x": "y" * 17000})
        self.assertEqual(resp.status_code, 200)

        resp = self.kphp_server.http_get(
            headers={"x": "y" * 70000})
        self.assertEqual(resp.status_code, 431)

        resp = self.kphp_server.http_get(
            headers={"x" * 5000: "y" * 70000})
        self.assertEqual(resp.status_code, 400)

        resp = self.kphp_server.http_get(
            uri="/a?{}".format("a" * 15000),
            headers={"x" * 4000: "y" * 15000})
        self.assertEqual(resp.status_code, 200)

        resp = self.kphp_server.http_get(
            uri="/a?{}".format("a" * 15000),
            headers={
                "x" * 4000: "a" * 15000,
                "y" * 4000: "b" * 15000,
            })
        self.assertEqual(resp.status_code, 200)

        resp = self.kphp_server.http_get(
            uri="/a?{}".format("a" * 15000),
            headers={
                "x" * 4000: "a" * 15000,
                "y" * 4000: "b" * 15000,
                "z" * 4000: "c" * 15000,
            })
        self.assertEqual(resp.status_code, 431)

    def test_post_limit(self):
        resp = self.kphp_server.http_post(data="x" * 4000)
        self.assertEqual(resp.status_code, 200)

        # no limits?
        resp = self.kphp_server.http_post(data="x" * 80000000)
        self.assertEqual(resp.status_code, 200)
