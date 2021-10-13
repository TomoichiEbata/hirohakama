/////////////////////////////////////////
//
// postGISを使って、自動車を走らせてみよう
//

/*

gcc -g -I"C:\OSGeo4W64\include" test-postgis.cpp -o test-postgis.exe
-L"C:\OSGeo4W64\lib" -llibpq -lwsock32 -lws2_32

*/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "libpq-fe.h"

struct BUS {
    double p_x;  // 現在のX座標
    double p_y;  // 現在のY座標
};

#define rad2deg(a) ((a) / M_PI * 180.0) /* rad を deg に換算するマクロ関数 */
#define deg2rad(a) ((a) / 180.0 * M_PI) /* deg を rad に換算するマクロ関数 */

double distance_km(double a_longitude, double a_latitude, double b_longitude,
                   double b_latitude, double *rad_up) {
    double earth_r = 6378.137;

    double loRe = deg2rad(b_longitude - a_longitude);  // 東西  経度は135度
    double laRe = deg2rad(b_latitude - a_latitude);  // 南北  緯度は34度39分

    double EWD = cos(deg2rad(a_latitude)) * earth_r * loRe;  // 東西距離
    double NSD = earth_r * laRe;                             //南北距離

    double distance = sqrt(pow(NSD, 2) + pow(EWD, 2));
    *rad_up = atan2(NSD, EWD);

    return distance;
}

double diff_longitude(double diff_p_x, double latitude) {
    double earth_r = 6378.137;
    // ↓ これが正解だけど、
    double loRe = diff_p_x / earth_r / cos(deg2rad(latitude));  // 東西
    // 面倒なので、これで統一しよう(あまり差が出ないしね)
    // double loRe = diff_p_x / earth_r / cos(deg2rad(35.700759)); // 東西
    double diff_lo = rad2deg(loRe);  // 東西

    return diff_lo;  // 東西
}

double diff_latitude(double diff_p_y) {
    double earth_r = 6378.137;
    double laRe = diff_p_y / earth_r;  // 南北
    double diff_la = rad2deg(laRe);    // 南北

    return diff_la;  // 南北
}

int main() {
    // postGISとQGISで調べたノード番号(強制停車するバス停を擬制)
    int bus_stop_num[] = {338, 5, 11, 20, -1};

    // const char *conninfo = "host=localhost user=postgres password=c-anemone
    // dbname=city_routing";
    const char *conninfo =
        "host=localhost port=15432 user=postgres password=password "
        "dbname=hiro_db";

    // データベースとの接続を確立する
    PGconn *conn = PQconnectdb(conninfo);
    PGresult *res;

    // バックエンドとの接続確立に成功したかを確認する
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s",
                PQerrorMessage(conn));
    }

    int cnt = 0;
    int initial_flag = 1;

    BUS test_bus;  // バスの生成

    while (bus_stop_num[cnt] != -1) {  // バス停単位の開始
        // printf("%d\n", bus_stop_num[cnt]);

        // SQL文字列格納用の配列
        char stringSQL[1024] = {0};

        // バス停とバス停の間のノードをダイクストラで計算する(node(source)とedge(gid)を取る)

        sprintf(stringSQL,
                "SELECT node, edge FROM pgr_dijkstra('SELECT gid as id, "
                "source, target, cost, reverse_cost AS "
                "reverse_cost FROM ways', %d, %d, true );",
                bus_stop_num[cnt], bus_stop_num[cnt + 1]);

        // printf("%s\n", stringSQL);

        res = PQexec(conn, stringSQL);

        if (res == NULL) {
            fprintf(stderr, "failed: %s", PQerrorMessage(conn));
        }

        // SELECTの場合戻り値は PGRES_TUPLES_OK.  これを確認しておく
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQerrorMessage(conn);
        }

        int nFields = PQnfields(res);

        // バス停からバス停の間のノード番号の一つをゲットする処理
        for (int i = 0; i < PQntuples(res) - 1; i++) {
            // バス停とバス停の間のノードを全部出す

            /*
            ここでは、両端のエッジの座標が必要になる。
            pgr_dijkstraでは、エッジ(両端)情報(x1,y1,x2,y2)が取得できるのだけど、
            どっちが先端でどっちが終端かが分からない。

            そこで(恐しく迂遠だけえど)、まずエッジ情報(x1,y1,x2,y2)を得てから、
            ノード情報(x1, y1)を取得する(ちなみにノード情報のx2,y2は、
            交差点などの場合は複数出てくるので、信用してはならない)。

            で、ノード情報のx1,y1を先端として、エッジ情報のx1,y1とx2,y2と一致していない方を終端とする
            という処理を取っている。

            (もっと簡単な方法があったら、誰か教えて下さい)
        */

            double node[2] = {0.0};  // x1, y1
            double edge[4] = {0.0};  // x1, y1, x2, y2

            int dummy_int_1 = 0;
            int dummy_int_2 = 0;

            for (int j = 0; j < nFields; j++) {
                // (j=0:node(source)と j=1:edge(gid)の値をゲットする)

                // まずノードの方
                if (j == 0) {  //(j=0:node(source)

                    memset(stringSQL, 0, sizeof(stringSQL));  // 念の為クリア
                    sprintf(stringSQL,
                            "SELECT x1,y1 from ways where source = %s;",
                            PQgetvalue(res, i, j));  // ノードの座標を得る

                    dummy_int_1 = atof(PQgetvalue(res, i, j));

                    PGresult *res2 = PQexec(conn, stringSQL);
                    if (res2 == NULL) {
                        fprintf(stderr, "failed: %s", PQerrorMessage(conn));
                    }

                    int nFields2 = PQnfields(res2);

                    for (int j2 = 0; j2 < nFields2; j2++) {
                        // node(source)のx1,y1の2つの値

                        // printf("%-15s", PQgetvalue(res2, 0, j2));
                        node[j2] = atof(PQgetvalue(res2, 0, j2));
                    }

                    PQclear(res2);  // SQL文の発行に対して必ずクリアする
                }

                //次にエッジの方
                if (j == 1) {  //(j=1:edge(gid)

                    memset(stringSQL, 0, sizeof(stringSQL));  // 念の為クリア

                    sprintf(stringSQL,
                            "SELECT x1,y1,x2,y2 from ways where gid = %s;",
                            PQgetvalue(res, i, j));
                    // ノードの座標を得る

                    dummy_int_2 = atof(PQgetvalue(res, i, j));

                    PGresult *res2 = PQexec(conn, stringSQL);
                    if (res2 == NULL) {
                        fprintf(stderr, "failed: %s", PQerrorMessage(conn));
                    }

                    int nFields2 = PQnfields(res2);

                    for (int j2 = 0; j2 < nFields2; j2++) {
                        // node(source)のx1,y1の2つの値
                        // printf("%-15s", PQgetvalue(res2, 0, j2));
                        edge[j2] = atof(PQgetvalue(res2, 0, j2));
                    }

                    PQclear(res2);  // SQL文の発行に対して必ずクリアする
                }
            }

            double start_x, start_y, end_x, end_y;

            //出揃ったところで、始点と終点の判定を行う
            if ((fabs(node[0] - edge[0]) < 1e-6) &&
                (fabs(node[1] - edge[1]) < 1e-6)) {
                start_x = edge[0];
                start_y = edge[1];
                end_x = edge[2];
                end_y = edge[3];
            } else {
                start_x = edge[2];
                start_y = edge[3];
                end_x = edge[0];
                end_y = edge[1];
            }

            // printf("両端の確定 %f,%f,%f,%f\n", start_x, start_y, end_x,
            // end_y);

            //両端の進行方向(ベクトル)を計算する
            double rad_up1;
            distance_km(start_x, start_y, end_x, end_y, &rad_up1);

            // 確定直後にバス位置は、出発点(start_x, start_y)に強制移動する
            test_bus.p_x = start_x;
            test_bus.p_y = start_y;

            // ここから0.1m単位でグルグル回す

            int do_flag = 0;
            do {
                // printf("x=%-15f,y=%-15f\n",test_bus.p_x, test_bus.p_y);
                printf("%-15f,%-15f\n", test_bus.p_x, test_bus.p_y);

                // 以下の0.1は1サンプリング0.01メートルを擬制
                test_bus.p_x +=
                    diff_longitude(0.1 * cos(rad_up1), test_bus.p_y);
                test_bus.p_y += diff_latitude(0.1 * sin(rad_up1));

                double rad0 = atan2((end_y - start_y), (end_x - start_x));
                double rad1 =
                    atan2((end_y - test_bus.p_y), (end_x - test_bus.p_x));

                // ここは、http://kobore.net/over90.jpg で解説してある

                if (fabs(rad0 - rad1) >= 3.141592654 * 0.5) {
                    // 終点越えの場合、終点に座標を矯正する
                    test_bus.p_x = end_x;
                    test_bus.p_y = end_y;

                    do_flag = 1;  // フラグを上げろ
                }

            } while (do_flag == 0);

        }  // バス停とバス停の間のノードを全部出す

        PQclear(res);  // SQL文の発行に対して必ずクリアする

        cnt += 1;  // カウントアップ

    }  // バス停単位の開始
}