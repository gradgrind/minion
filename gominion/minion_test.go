package gominion

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestM1(t *testing.T) {
	fmt.Println("\n +++ TestM1:\n ==============")
	doc := ReadMinion("{A: 1 :}")
	if e, ok := doc.(MError); ok {
		fmt.Println(" *** Error ***")
		fmt.Printf("=== %s\n", e)
	} else {
		fmt.Println("  -->")
		fmt.Printf("%#v\n", doc)
		fmt.Println(DumpMinion(doc, -1))
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
		t0      time.Time
		t1      time.Time
		t2      time.Time
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
			v := ReadMinion(input)
			t1 = time.Now()
			DumpMinion(v, -1)
			t2 = time.Now()
			fmt.Printf("== %s: %s\n", filepath.Base(fin), t1.Sub(t0))
			fmt.Printf("  (Dump) %s\n", t2.Sub(t1))
		}
		fmt.Println("  ---")
	}
	fmt.Println("  ---------------------------------- ")

	doc := ReadMinion(input)

	if e, ok := doc.(MError); ok {
		fmt.Println(" *** Error ***")
		fmt.Println(e)
	} else {
		fmt.Println("  -->")
		fmt.Println(DumpMinion(doc, 0))
	}
}
