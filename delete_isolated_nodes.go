// delete_isolated_nodes.go

// mapから孤立ノードを削除する処理 (環境構築に使うのみ)
//
// 使い方:
//   go get github.com/lib/pq
//
//   go run delete_isolated_nodes.go [-p PORT]
//     または
//   go build delete_isolated_nodes.go
//   ./delete_isolated_nodes [-p PORT]
//
// 機能:
//   1. 出発ノードからエッジを辿って、たどり着けるところを全スキャンする。
//      (エッジで繋がっている隣接ノードを塗りつぶす、を再帰的に繰り返すイメージ。)
//   2. 全スキャンによって、たどり着けなかったノード(未到達ノード)を、削除対象とする。
//      (1.で塗りつぶせなかったノード ＝ 孤立しているノード)
//   3. 削除対象が正常(半分以下)であれば、削除を実行する。
//      削除対象が半分以上(多すぎる)の場合は、出発ノードを変更して全スキャンをやり直す。
//      (後者は、孤立ノードからスキャンを始めてしまった場合に該当。)
//
// オプション:
//   go run delete_isolated_nodes.go --help  #を参照
//
// 同じことを、control/environment/delete_isolated_nodes.sqlでも(効率悪い方法で)
// 行っていたが、[3.]の判定に対応していないため、削除対象が多すぎる場合でも
// 削除を実行してしまう。

//go:build ignore
// +build ignore

package main

import (
	"flag"
	"fmt"
	"os"

	"database/sql"

	_ "github.com/lib/pq"
)

func MakeNodemap(db *sql.DB) (map[int]bool, []int, error) {
	// カウントを取得して、メモリを確保
	var nodeCount int

	{
		sql := "SELECT count(id) FROM ways_vertices_pgr;"
		if err := db.QueryRow(sql).Scan(&nodeCount); err != nil {
			fmt.Printf("error : %v", err)
			os.Exit(1)
		}
	}

	nodelist := make([]int, nodeCount)
	nodemap := make(map[int]bool, nodeCount)

	// 行毎の値を取得
	sql := "SELECT id FROM ways_vertices_pgr;"

	rows, err := db.Query(sql)
	if err != nil {
		return nil, nil, err
	}
	listIndex := 0
	for rows.Next() {
		var id int
		rows.Scan(&id)
		nodelist[listIndex] = id
		nodemap[id] = false
		listIndex += 1
	}
	return nodemap, nodelist, nil
}

func MakeEdgemap(db *sql.DB) (map[int][]int, error) {
	// SQLステートメント
	sql := "SELECT source, target FROM ways;"
	prepared, err := db.Prepare(sql)
	rows, err := prepared.Query()

	if err != nil {
		return nil, err
	}
	edges := make(map[int][]int)

	for rows.Next() {
		var source int
		var target int
		rows.Scan(&source, &target)

		if source == target {
			continue
		}

		if val, ok := edges[source]; ok {
			edges[source] = append(val, target)
		} else {
			edges[source] = append(make([]int, 0), target)
		}

		if val, ok := edges[target]; ok {
			edges[target] = append(val, source)
		} else {
			edges[target] = append(make([]int, 0), source)
		}
	}
	return edges, nil
}

// 未チェックのノードを再帰的に走査してチェックする
// Returns: チェック済みの数
func ScanUnchecked(source int, edges map[int][]int, nodes map[int]bool) int {
	// エラーチェック: 存在しない
	if _, ok := nodes[source]; !ok {
		return 0
	}
	//
	countChecked := 1 // 自分自身の分(+1)
	nodes[source] = true
	// fmt.Println(edges[source])
	for _, target := range edges[source] {
		if (nodes)[target] == false {
			// fmt.Println("target:", target, " <- ", source)
			countChecked += ScanUnchecked(target, edges, nodes)
		}
	}
	return countChecked
}

func DeleteNodes(db *sql.DB, nodeIDs []int) error {
	if len(nodeIDs) == 0 {
		return nil
	}
	sqlDeleteW := "DELETE FROM ways WHERE gid IN (SELECT gid FROM ways WHERE source = $1 OR target = $1);"
	preparedW, err := db.Prepare(sqlDeleteW)
	if err != nil {
		return err
	}
	sqlDeleteV := "DELETE FROM ways_vertices_pgr WHERE id = $1;"
	preparedV, err := db.Prepare(sqlDeleteV)
	if err != nil {
		return err
	}

	for _, i := range nodeIDs {
		_, err := preparedW.Exec(i)
		if err != nil {
			return err
		}
	}
	for _, i := range nodeIDs {
		_, err := preparedV.Exec(i)
		if err != nil {
			return err
		}
	}
	return nil
}

func main() {
	// 実行時フラグの定義
	dbPort := flag.Int("p", 15432, "DBアクセス用のポート番号。\nDocker内なら、通常は5432のままで問題無し(15432を使っている)")
	defaultStartID := flag.Int("i", -1, "スキャンを開始する出発ノードのIDを、直接指定(固定)。\n値が-1の場合は、自動探索。")
	dryrun := flag.Bool("n", false, "削除予定のノード数を表示して終了。削除は実行しない (dryrun)。")
	flag.Parse()

	// db: データベースに接続するためのハンドラ
	var db *sql.DB
	// Dbの初期化

	dbParam := fmt.Sprintf("host=localhost port=%d user=postgres password=password dbname=hiro_db sslmode=disable", *dbPort)
	db, err := sql.Open("postgres", dbParam)
	if err != nil {
		fmt.Println("DBを開けません。")
		os.Exit(1)
	}

	// 既存の全ノードのIDを取得
	nodeflags, nodeIDList, _ := MakeNodemap(db)
	// ノード対を結ぶエッジを取得
	edges, _ := MakeEdgemap(db)
	totalCount := len(nodeflags)

	if len(nodeflags) == 0 {
		fmt.Println("ノードが0個です。")
		os.Exit(1)
	}

	// もしもID指定があるなら、差し替え
	if *defaultStartID >= 0 {
		nodeIDList = []int{*defaultStartID} // 1要素
		// startID = nodeIDList[0] // 1番目
	}

	ok := false
	// スキャン開始
	for _, startID := range nodeIDList {

		// フラグのリセット
		for key := range nodeflags {
			nodeflags[key] = false
		}

		// fmt.Println("Source count:", totalCount, " Scan start at:")
		// 再帰スキャンの実行
		checkedCount := ScanUnchecked(startID, edges, nodeflags)
		uncheckedCount := totalCount - checkedCount

		fmt.Printf("[ノード#%dからのスキャン]  到達数: %d  未到達: %d\n",
			startID, checkedCount, uncheckedCount)

		// やり直し
		if uncheckedCount >= checkedCount {
			fmt.Println("(到達数が、未到達の数以下)")
			continue
		}

		uncheckedIDs := make([]int, 0, uncheckedCount)

		for id, checked := range nodeflags {
			if checked == false {
				uncheckedIDs = append(uncheckedIDs, id)
			}
		}
		fmt.Printf("削除対象のノード (%d個):\n%v\n", len(uncheckedIDs), uncheckedIDs)

		if *dryrun {
			fmt.Printf("dryrunのため、削除を実行しません。\n")
		} else if len(uncheckedIDs) == 0 {
			fmt.Printf("削除対象がないので、削除を実行しません。\n")
		} else {
			fmt.Printf("削除を実行します。\n")
			err := DeleteNodes(db, uncheckedIDs)
			if err != nil {
				fmt.Println("Error:", err)
				os.Exit(1)
			}
		}

		//
		ok = true
		break
	}

	if ok == false {
		fmt.Printf("孤立ノードの削除に失敗しました。\n")
		os.Exit(1)
	}
}
