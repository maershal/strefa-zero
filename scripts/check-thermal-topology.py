"""Demonstrate why a single wall bit at 160x96 cannot preserve fine topology."""
import argparse,json
from collections import deque
from pathlib import Path
def connected(walls,width,height,start,end):
    seen={start};todo=deque([start])
    while todo:
        x,y=todo.popleft()
        if (x,y)==end:return True
        for p in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if 0<=p[0]<width and 0<=p[1]<height and p not in walls and p not in seen:
                seen.add(p);todo.append(p)
    return False
def case(door):
    walls={(160,y)for y in range(192)}
    if door:walls.remove((160,90))
    result={'fine_320x192':connected(walls,320,192,(80,90),(240,90))}
    for name,rule in (('coarse_OR',any),('coarse_AND',all)):
        coarse={(x,y)for y in range(96)for x in range(160)if rule((2*x+dx,2*y+dy)in walls for dy in range(2)for dx in range(2))}
        result[name]=connected(coarse,160,96,(40,45),(120,45))
    return result
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    result={'closed_wall':case(False),'single_cell_door':case(True),'selected_grid':[320,192],'scope':'Connectivity experiment, not a coarse-grid fluid benchmark.'}
    assert result['closed_wall']=={'fine_320x192':False,'coarse_OR':False,'coarse_AND':True}
    assert result['single_cell_door']=={'fine_320x192':True,'coarse_OR':False,'coarse_AND':True}
    args.output.write_text(json.dumps(result,indent=2)+'\n');print('Topology controls: PASS')
if __name__=='__main__':main()
