#pragma once
namespace ink {
// WMO weather codes -> pinned asset order (see generator manifest).
constexpr int weatherIconIndex(int code,bool day) {
  if(code==0)return day?0:1;
  if(code==1||code==2)return day?2:3;
  if(code==45||code==48)return 5;
  if(code==56||code==57||code==66||code==67)return 10;
  if(code==75||code==86)return 9;
  if((code>=71&&code<=77)||code==85)return 8;
  if(code==96||code==99)return 12;
  if(code==95)return 11;
  if(code==65||code==82)return 7;
  if((code>=51&&code<=55)||(code>=61&&code<=63)||code==80||code==81)return 6;
  return 4;
}
} // namespace ink
