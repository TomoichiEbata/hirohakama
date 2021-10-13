// test-position.go
//
// node番号 338, 5, 11, 20 を経由する最短経路のnodeを書き出すだけのプログラム
//
// $ go mod init m
// $ go get github.com/lib/pq
// $ go run test-position.go

package main

import (
	"database/sql"
	"fmt"
	"log"
	"os"

	_ "github.com/lib/pq"
)

const port int = 15432 // DBコンテナが公開しているポート番号

func main() {
	// db: データベースに接続するためのハンドラ
	var db *sql.DB
	// Dbの初期化
	dbParam := fmt.Sprintf("host=localhost port=%d user=postgres password=password dbname=hiro_db sslmode=disable", port)
	db, err := sql.Open("postgres", dbParam)
	if err != nil {
		fmt.Println("cannot open db")
		os.Exit(1)
	}
	defer db.Close()

	bus_stop_num := []int{338, 5, 11, 20}

	for i := 0; i < 3; i++ {

		// select から選ばれるのは、nodeだけにすること(他の要素は使わないこと) rows.Next() でデータを取る場合には、select 要素だけで取られていくので注意
		// rows.Scan()は、"node"という変数名で選ばれている訳ではない、ということ
		stringSQL := fmt.Sprintf("select node FROM pgr_dijkstra('SELECT gid as id, source, target,length as cost FROM ways',%d, %d);", bus_stop_num[i], bus_stop_num[i+1])

		rows, err := db.Query(stringSQL)
		if err != nil {
			log.Println(err)
		}
		defer rows.Close()

		for rows.Next() {
			var node1 int64 // "node"という変数名にしても関係ない
			rows.Scan(&node1)
			fmt.Println(node1)
		}
		fmt.Printf("\n")
	}
}
