# About this repository

This repository is a collection of posts that reflect as blog posts and article on [pyjamabrah.com](https://pyjamabrah.com).

# How to Contribute?

You can contribute technical writings to [pyjamabrah.com](https://pyjamabrah.com) by -
1. adding a directory to this repository and then within that add a `index.md`
1. The `index.md` is where you will write your article.
1. Raise a pull request once your article is ready for review.
1. We will review and merge the changes and the article should go live on [pyjamabrah.com](https://pyjamabrah.com).

Following sections guide on how to format your article. In general, you are free to format it any which way. There is some mandatory metadata that needs to be added on the top of the file so the build system for pyjamabrah.com processes it correctly.

## Setting up the Directory

Say you wanted to write on `foo` as the topic. Simply copy `sample/` to `foo/` and follow the instructions in the `index.md` file within it.

```shell
foo/
  └── index.md
```

## A word about `index.md`

The file includes front matter at the top of the file which looks like yaml sytax (which it is btw) -

```yaml
---
# Change the Date below
date: "2025-02-16"

# Add the title of the Post here
title: 'Title of the Post'

# replace the "sample/cover.png" with path to a 16:9 image
# this image will be shown in SEO optimization and when
# you share the post on social media.
thumbnail: "/posts/sample/cover.png"

# Add your GitHub handle here
author: ""

# Add tags as suitable for the topic
tags:
  - ""

# If it fits a certain category of topics, add that
categories:
  - ""

# If this is a Series of posts, Add the name of the series
series:
  - ""

# Delete the line below when submitting for review
draft: true
---
```

Edit the section as guided in the comments above each field/line. This part of the file, the parser uses to infer the information like title, date, SEO image etc.

The content should go after this section. Write the article using markdown format.


## Summary of the Post

You will notice the `<!--more-->` marker in the `index.md` file. DO NOT DELETE IT! The text above the marker is used as summary which shows up on the website as well as in the SEO. `index.md` file has instructions on how to handle it.

## Images

All images to be included in the article should go within the directory you created. In this case `foo`.

### In the front matter

The `thumbnail` key in the front matter of the article should always start with `/posts/` followed by the path to cover image in the directory you created. In the current case it will be `foo/cover.png`. The full path then becomes `/posts/foo/cover.png`.

There are no restrictions on what the image has to be named. It can be something different than `cover.png` as well. Whatever it is, use that.

### Images in the Article

The images in the body of the article don't have to have the complicated paths. You can use the local path like `cover.png`. You can use the following syntax to include an image and add a caption. The caption is optional.

```markdown
![](local-image.png "caption goes here")
```

The image will be added in place.

## Markdown format

Besides the instructions above, you can use the [markdown syntax highlighted here](https://www.markdownguide.org/cheat-sheet/)


## Contact us

Need more help/guidance? Reach us on [support@pyjamacafe.com](mailto:support@pyjamacafe.com)
