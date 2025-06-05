package minion

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestM1(t *testing.T) {
	fmt.Println("\n +++ TestM1:\n ==============")
	ib := InputBuffer{}
	doc, e := ib.Read("{A: 1 :}")
	if len(e) == 0 {
		fmt.Println("  -->")
		dump := Dumper()
		fmt.Println(dump(doc, -1))
	} else {
		fmt.Println(" *** Error ***")
		fmt.Println(e)
	}
}

var infiles = []string{
	"../data/test1.minion",
	//"../data/test1a.minion",
	"../data/test2.minion",
	//"../data/test2a.minion",
	//"../data/test2e.minion",
	"../data/test3.minion",
	"../data/test4.minion",
	//"../data/test4e.minion",
	//
}

func TestM2(t *testing.T) {
	fmt.Println("\n +++ TestM2:\n ==============")
	var (
		input   string
		ib      InputBuffer
		t0      time.Time
		t1      time.Time
		content []byte
		err     error
	)
	for range 10 {
		for _, fin := range infiles {
			content, err = os.ReadFile(fin)
			if err != nil {
				fmt.Println("$ File Error: " + err.Error())
				return
			}
			input = string(content)
			t0 = time.Now()
			ib = InputBuffer{}
			ib.Read(input)
			t1 = time.Now()
			fmt.Printf("== %s: %s\n", filepath.Base(fin), t1.Sub(t0))
		}
		fmt.Println("  ---")
	}
	fmt.Println("  ---------------------------------- ")

	doc, e := ib.Read(input)

	if len(e) == 0 {
		fmt.Println("  -->")
		dump := Dumper()
		fmt.Println(dump(doc, 0))
	} else {
		fmt.Println(" *** Error ***")
		fmt.Println(e)
	}
}
