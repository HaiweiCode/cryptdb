#!/usr/bin/ruby

require 'rubygems'
require 'mysql'

# wordpress database prep
# FIXME: some onions are missing
# > ones that lowered something in addition to oPLAIN

$onions =
[["wp_commentmeta", "meta_id", "oPLAIN", "PLAINVAL"],
 ["wp_commentmeta", "meta_key", "oDET", "DET"],
 ["wp_commentmeta", "meta_value", "oOPE", "OPE"],
 ["wp_commentmeta", "comment_id", "oDET", "DET"],
 ["wp_comments", "comment_ID", "oPLAIN", "PLAINVAL"],
 ["wp_comments", "comment_author_url", "oDET", "DET"],
 ["wp_comments", "comment_approved", "oDET", "DET"],
 ["wp_comments", "comment_date", "oPLAIN", "PLAINVAL"],
 ["wp_comments", "comment_date_gmt", "oPLAIN", "PLAINVAL"],
 ["wp_comments", "comment_parent", "oDET", "DET"],
 ["wp_comments", "comment_post_ID", "oDET", "DET"],
 ["wp_comments", "comment_post_ID", "oPLAIN", "PLAINVAL"],
 ["wp_comments", "user_id", "oDET", "DET"],
 ["wp_links", "link_id", "oPLAIN", "PLAINVAL"],
 ["wp_links", "link_updated", "oPLAIN", "PLAINVAL"],
 ["wp_options", "autoload", "oDET", "DET"],
 ["wp_options", "option_id", "oPLAIN", "PLAINVAL"],
 ["wp_options", "option_name", "oDET", "DET"],
 ["wp_options", "option_name", "oPLAIN", "PLAINVAL"],
 ["wp_postmeta", "meta_id", "oPLAIN", "PLAINVAL"],
 ["wp_postmeta", "meta_key", "oPLAIN", "PLAINVAL"],
 ["wp_postmeta", "meta_key", "oDET", "DET"],
 ["wp_postmeta", "meta_key", "oOPE", "OPE"],
 ["wp_postmeta", "meta_value", "oDET", "DET"],
 ["wp_postmeta", "meta_value", "oOPE", "OPE"],
 ["wp_postmeta", "post_id", "oPLAIN", "PLAINVAL"],
 ["wp_postmeta", "post_id", "oDET", "DET"],
 ["wp_posts", "ID", "oPLAIN", "PLAINVAL"],
 ["wp_posts", "menu_order", "oOPE", "OPE"],
 ["wp_posts", "post_author", "oDET", "DET"],
 ["wp_posts", "post_date", "oPLAIN", "PLAINVAL"],
 ["wp_posts", "post_date_gmt", "oPLAIN", "PLAINVAL"],
 ["wp_posts", "post_mime_type", "oDET", "DET"],
 ["wp_posts", "post_modified", "oPLAIN", "PLAINVAL"],
 ["wp_posts", "post_modified_gmt", "oPLAIN", "PLAINVAL"],
 ["wp_posts", "post_name", "oDET", "DET"],
 ["wp_posts", "post_name", "oPLAIN", "PLAINVAL"],
 ["wp_posts", "post_parent", "oDET", "DET"],
 ["wp_posts", "post_parent", "oOPE", "OPE"],
 ["wp_posts", "post_status", "oDET", "DET"],
 ["wp_posts", "post_title", "oOPE", "OPE"],
 ["wp_posts", "post_type", "oDET", "DET"],
 ["wp_posts", "to_ping", "oDET", "DET"],
 ["wp_term_relationships", "object_id", "oDET", "DET"],
 ["wp_term_relationships", "object_id", "oPLAIN", "PLAINVAL"],
 ["wp_term_relationships", "term_taxonomy_id", "oDET", "DET"],
 ["wp_term_relationships", "term_taxonomy_id", "oPLAIN", "PLAINVAL"],
 ["wp_term_taxonomy", "count", "oOPE", "OPE"],
 ["wp_term_taxonomy", "taxonomy", "oDET", "DET"],
 ["wp_term_taxonomy", "term_id", "oPLAIN", "PLAINVAL"],
 ["wp_term_taxonomy", "term_id", "oDET", "DET"],
 ["wp_term_taxonomy", "term_taxonomy_id", "oPLAIN", "PLAINVAL"],
 ["wp_terms", "name", "oDET", "DET"],
 ["wp_terms", "name", "oOPE", "OPE"],
 ["wp_terms", "slug", "oDET", "DET"],
 ["wp_terms", "term_id", "oPLAIN", "PLAINVAL"],
 ["wp_usermeta", "meta_key", "oDET", "DET"],
 ["wp_usermeta", "meta_value", "oPLAIN", "PLAINVAL"],
 ["wp_usermeta", "umeta_id", "oPLAIN", "PLAINVAL"],
 ["wp_usermeta", "user_id", "oPLAIN", "PLAINVAL"],
 ["wp_usermeta", "user_id", "oDET", "DET"],
 ["wp_users", "ID", "oPLAIN", "PLAINVAL"],
 ["wp_users", "display_name", "oOPE", "OPE"],
 ["wp_users", "user_email", "oDET", "DET"],
 ["wp_users", "user_login", "oDET", "DET"],
 ["wp_users", "user_login", "oOPE", "OPE"],
 ["wp_users", "user_nicename", "oDET", "DET"],
 ["wp_users", "user_registered", "oPLAIN", "PLAINVAL"],
 ["wp_users", "user_url", "oPLAIN", "PLAINVAL"]]

def fn(database)
    c = Mysql.new('127.0.0.1', 'root', 'letmein', database, 3305)
    c.query("USE `#{database}`")
    $onions.each do |(table, field, onion, level)|
    c.query("SET @cryptdb='adjust', @database='#{database}',
                 @table='#{table}', @field='#{field}',
                 @#{onion}='#{level}'")
    end
    c.close()
end

fn("burrows+gg")
